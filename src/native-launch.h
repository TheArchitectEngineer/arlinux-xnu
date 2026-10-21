/* Host-side launch configuration. A helper never becomes a GUI application
 * merely because its parent exported WAYLAND_DISPLAY. */
#pragma once
enum native_launch_role { NATIVE_CONSOLE, NATIVE_APPLICATION, NATIVE_SERVICE };
struct native_launch_options {
    const char *program,*dyld,*root,*container,*service_name,*service_instance;
    enum native_launch_role role;
};
static struct native_launch_options native_launch_parse(int argc,char **argv)
{
    struct native_launch_options options={0};
    int role_seen=0;
    for (int i=1;i<argc;i++) {
        const char *argument=argv[i];
        if (!strcmp(argument,"--dyld") || !strcmp(argument,"--root") || !strcmp(argument,"--role") || !strcmp(argument,"--container") ||
            !strcmp(argument,"--service-name") || !strcmp(argument,"--service-instance")) {
            need(i+1<argc,"launch option requires a value");
            const char *value=argv[++i];
            if (!strcmp(argument,"--dyld")) { need(!options.dyld,"duplicate --dyld"); options.dyld=value; }
            else if (!strcmp(argument,"--root")) { need(!options.root,"duplicate --root"); options.root=value; }
            else if (!strcmp(argument,"--container")) { need(!options.container,"duplicate --container"); options.container=value; }
            else if (!strcmp(argument,"--service-name")) { need(!options.service_name,"duplicate --service-name"); options.service_name=value; }
            else if (!strcmp(argument,"--service-instance")) { need(!options.service_instance,"duplicate --service-instance"); options.service_instance=value; }
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
