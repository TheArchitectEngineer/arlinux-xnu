/* Host-side launch configuration. A helper never becomes a GUI application
 * merely because its parent exported WAYLAND_DISPLAY. */
#pragma once
#include <plist/plist.h>
enum native_launch_role { NATIVE_CONSOLE, NATIVE_APPLICATION, NATIVE_SERVICE };
struct native_launch_options {
    const char *program,*dyld,*root,*container,*group_root,*service_name,*service_instance,*service_index;
    enum native_launch_role role;
};
static struct native_launch_options native_launch_parse(int argc,char **argv)
{
    struct native_launch_options options={0};
    int role_seen=0;
    for (int i=1;i<argc;i++) {
        const char *argument=argv[i];
        if (!strcmp(argument,"--dyld") || !strcmp(argument,"--root") || !strcmp(argument,"--role") || !strcmp(argument,"--container") || !strcmp(argument,"--group-root") ||
            !strcmp(argument,"--service-name") || !strcmp(argument,"--service-instance") || !strcmp(argument,"--service-index")) {
            need(i+1<argc,"launch option requires a value");
            const char *value=argv[++i];
            if (!strcmp(argument,"--dyld")) { need(!options.dyld,"duplicate --dyld"); options.dyld=value; }
            else if (!strcmp(argument,"--root")) { need(!options.root,"duplicate --root"); options.root=value; }
            else if (!strcmp(argument,"--container")) { need(!options.container,"duplicate --container"); options.container=value; }
            else if (!strcmp(argument,"--group-root")) { need(!options.group_root,"duplicate --group-root"); options.group_root=value; }
            else if (!strcmp(argument,"--service-name")) { need(!options.service_name,"duplicate --service-name"); options.service_name=value; }
            else if (!strcmp(argument,"--service-instance")) { need(!options.service_instance,"duplicate --service-instance"); options.service_instance=value; }
            else if (!strcmp(argument,"--service-index")) { need(!options.service_index,"duplicate --service-index"); options.service_index=value; }
            else {
                need(!role_seen,"duplicate --role"); role_seen=1;
                if (!strcmp(value,"application")) options.role=NATIVE_APPLICATION;
                else if (!strcmp(value,"service")) options.role=NATIVE_SERVICE;
                else need(!strcmp(value,"console"),"unknown process role");
            }
        } else {
            need(argument[0]!='-' && !options.program,"unknown launch option or multiple programs");
            options.program=argument;
        }
    }
    need(options.program!=NULL,"missing ARM64 Mach-O program");
    need(!options.root || options.dyld,"--root requires --dyld");
    need(options.role==NATIVE_CONSOLE || options.root,"application/service requires an iOS root");
    need(options.role!=NATIVE_APPLICATION || options.container,"application requires its explicit data container");
    if(options.service_name || options.service_instance) {
        need(options.role==NATIVE_SERVICE && options.service_name && options.service_instance && options.container,
            "named service requires role, instance and container");
        need(*options.service_name && strlen(options.service_name)<128 && !strchr(options.service_name,'/') &&
             strlen(options.service_instance)==36,"invalid service identity");
    }
    return options;
}
static int native_container_directory(int parent,const char *name)
{
    if(mkdirat(parent,name,0700) && errno!=EEXIST)die("create application directory");
    int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)die("open application directory");
    return fd;
}
static char *native_launch_container(const char *path)
{
    if(!path)return NULL;
    char *canonical=realpath(path,NULL);
    if(!canonical)die("canonical application container");
    need(strlen(canonical)<4096 && strcmp(canonical,"/"),"application container path");
    int base=open(canonical,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(base<0)die("open application container");
    int library=native_container_directory(base,"Library");
    close(native_container_directory(base,"Documents"));
    close(native_container_directory(base,"tmp"));
    close(native_container_directory(library,"Caches"));
    close(native_container_directory(library,"Preferences"));
    close(native_container_directory(library,"Application Support"));
    close(library);close(base);
    fprintf(stderr,"NATIVE_CONTAINER path=%s\n",canonical);
    return canonical;
}
static void native_launch_groups(const char *path,const struct darwin_code_metadata *code)
{
    if(!path)return;
    char *canonical=realpath(path,NULL);
    if(!canonical)die("canonical group storage root");
    need(strcmp(canonical,"/") && strlen(canonical)<4096,"group root path");
    int root=open(canonical,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(root<0)die("open group storage root");
    /* libplist parses the original XML blob; no app IDs or inferred grants.
     * DER-only membership needs a decoder and remains unsupported here. */
    plist_t metadata=NULL;
    if(code->xml) {
        need(code->xml_bytes>8 && code->xml_bytes<=1024*1024,"XML entitlement bounds");
        plist_from_xml((const char *)code->xml+8,code->xml_bytes-8,&metadata);
        need(metadata && plist_get_node_type(metadata)==PLIST_DICT,"invalid entitlement dictionary");
    }
    plist_t groups=metadata ? plist_dict_get_item(metadata,"com.apple.security.application-groups") : NULL;
    if(groups) {
        need(plist_get_node_type(groups)==PLIST_ARRAY && plist_array_get_size(groups)<=32,"application groups bounds");
        for(uint32_t i=0;i<plist_array_get_size(groups);i++) {
            plist_t item=plist_array_get_item(groups,i);
            need(plist_get_node_type(item)==PLIST_STRING,"application group type");
            char *name=NULL;plist_get_string_val(item,&name);
            need(name && *name && strlen(name)<256 && strcmp(name,".") && strcmp(name,".."),"application group identifier");
            for(const unsigned char *p=(const unsigned char *)name;*p;p++)
                need((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='.' || *p=='-' || *p=='_',
                     "unsupported group identifier character");
            int group=native_container_directory(root,name);
            int library=native_container_directory(group,"Library");
            close(native_container_directory(library,"Caches"));close(library);close(group);
            char directory[4096];
            int n=snprintf(directory,sizeof(directory),"%s/%s",canonical,name);
            need(n>0 && (size_t)n<sizeof(directory),"group container path bounds");
            need(!darwin_native_group_register(name,directory),"register application group storage");
            fprintf(stderr,"NATIVE_GROUP_STORAGE identifier=%s path=%s\n",name,directory);
            free(name);
        }
    }
    if(metadata)plist_free(metadata);
    close(root);free(canonical);
}
