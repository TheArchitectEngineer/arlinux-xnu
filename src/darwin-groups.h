/* TLS-free access to explicit, real application-group storage. The launcher
 * registers only groups declared by the original executable's metadata. */
#pragma once
struct native_group {
    char identifier[D_NATIVE_GROUP_IDENTIFIER_LIMIT],path[4096];
    unsigned int path_length;
    int descriptor;
};
static struct native_group native_groups[D_NATIVE_GROUP_LIMIT];
static unsigned int native_group_count;
static int native_group_descriptor(int descriptor)
{
    for(unsigned int i=0;i<native_group_count;i++)
        if(native_groups[i].descriptor==descriptor)return 1;
    return 0;
}
/* Host libc/TLS is still active in this preparation API. */
int darwin_native_group_register(const char *identifier,const char *path)
{
    size_t id_length=strlen(identifier),path_length=strlen(path);
    if(!id_length || id_length>=D_NATIVE_GROUP_IDENTIFIER_LIMIT || !path_length || path_length>=4096)
        return -ENAMETOOLONG;
    for(unsigned int i=0;i<native_group_count;i++)
        if(!strcmp(native_groups[i].identifier,identifier))return -EEXIST;
    if(native_group_count==D_NATIVE_GROUP_LIMIT)return -E2BIG;
    int descriptor=open(path,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(descriptor<0)return -errno;
    struct native_group *group=&native_groups[native_group_count++];
    memcpy(group->identifier,identifier,id_length+1);memcpy(group->path,path,path_length+1);
    group->path_length=path_length;group->descriptor=descriptor;
    return 0;
}
static long native_group_path(const char *identifier,char *output,unsigned long capacity)
{
    char name[4096];
    long error=guest_path((unsigned long)identifier,name);if(error)return error;
    for(unsigned int i=0;i<native_group_count;i++) {
        struct native_group *group=&native_groups[i];
        unsigned int n=0;while(name[n] && name[n]==group->identifier[n])n++;
        if(name[n] || group->identifier[n])continue;
        if(capacity<=group->path_length)return -ERANGE;
        return guest_copy(group->path,(unsigned long)output,group->path_length+1,1);
    }
    return -ENOENT;
}
