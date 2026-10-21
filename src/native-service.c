/* Resolve real XPC bundle metadata from the caller's firmware view and execute
 * the original Mach-O with our native launcher. No workstation build, copied
 * home, invented executable, dummy service port or guest callback is involved. */
#define _GNU_SOURCE
#include "native-service.h"
#include "darwin-native.h"
#include <plist/plist.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct caller_launch { char arguments[32768];const char *root,*dyld,*container,*group_root,*service_index; };
static int caller_launch_read(uint32_t pid,struct caller_launch *launch)
{
    char path[64];struct stat caller,self;
    snprintf(path,sizeof(path),"/proc/%u/exe",pid);
    if(stat(path,&caller) || stat("/proc/self/exe",&self))return errno;
    if(caller.st_dev!=self.st_dev || caller.st_ino!=self.st_ino)return EPERM;
    snprintf(path,sizeof(path),"/proc/%u/cmdline",pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return errno;
    ssize_t count=read(fd,launch->arguments,sizeof(launch->arguments));int error=errno;close(fd);
    if(count<0)return error;
    if(!count || count==(ssize_t)sizeof(launch->arguments) || launch->arguments[count-1])return EINVAL;
    const char *last=launch->arguments+count;
    for(const char *arg=launch->arguments;arg<last;) {
        const char *value=arg+strlen(arg)+1;
        if(!strcmp(arg,"--root") || !strcmp(arg,"--dyld") || !strcmp(arg,"--container") || !strcmp(arg,"--group-root") || !strcmp(arg,"--service-index")) {
            if(value>=last)return EINVAL;
            if(!strcmp(arg,"--root"))launch->root=value;
            if(!strcmp(arg,"--dyld"))launch->dyld=value;
            if(!strcmp(arg,"--container"))launch->container=value;
            if(!strcmp(arg,"--group-root"))launch->group_root=value;
            if(!strcmp(arg,"--service-index"))launch->service_index=value;
            arg=value+strlen(value)+1;
        } else arg=value;
    }
    return launch->root && launch->dyld && launch->container && launch->service_index ? 0 : EINVAL;
}
static plist_t read_bundle(const char *path)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return NULL;
    struct stat file;
    if(fstat(fd,&file) || !S_ISREG(file.st_mode) || file.st_size<=0 || file.st_size>1024*1024){close(fd);return NULL;}
    char *bytes=malloc(file.st_size);size_t done=0;
    if(!bytes){close(fd);return NULL;}
    while(done<(size_t)file.st_size) {
        ssize_t n=read(fd,bytes+done,file.st_size-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        done+=n;
    }
    close(fd);plist_t result=NULL;
    if(done==(size_t)file.st_size) {
        if(done>=8 && !memcmp(bytes,"bplist00",8))plist_from_bin(bytes,done,&result);
        else plist_from_xml(bytes,done,&result);
    }
    free(bytes);
    if(result && plist_get_node_type(result)!=PLIST_DICT){plist_free(result);result=NULL;}
    return result;
}
static char *bundle_string(plist_t bundle,const char *key)
{
    plist_t item=plist_dict_get_item(bundle,key);char *value=NULL;
    if(item && plist_get_node_type(item)==PLIST_STRING)plist_get_string_val(item,&value);
    return value;
}
static int service_program(const char *root,const char *index,const char *service,char **output)
{
    char *canonical_root=realpath(root,NULL);if(!canonical_root)return errno;
    plist_t catalog=read_bundle(index);char *registered=catalog ? bundle_string(catalog,"root") : NULL;
    if(!registered || strcmp(registered,canonical_root)) {
        free(registered);if(catalog)plist_free(catalog);free(canonical_root);return EINVAL;
    }
    free(registered);
    plist_t services=plist_dict_get_item(catalog,"services");
    plist_t paths=services && plist_get_node_type(services)==PLIST_DICT ? plist_dict_get_item(services,service) : NULL;
    int error=ENOENT;char *found=NULL;
    if(paths && plist_get_node_type(paths)!=PLIST_ARRAY)error=EINVAL;
    uint32_t count=paths && plist_get_node_type(paths)==PLIST_ARRAY ? plist_array_get_size(paths) : 0;
    if(count>128){error=E2BIG;count=0;}
    for(uint32_t i=0;i<count;i++) {
            plist_t item=plist_array_get_item(paths,i);char *relative=NULL;
            if(item && plist_get_node_type(item)==PLIST_STRING)plist_get_string_val(item,&relative);
            if(!relative || !*relative || *relative=='/') {free(relative);error=EINVAL;break;}
            char path[PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",canonical_root,relative);free(relative);
            char *bundle_path=n>0 && n<(int)sizeof(path) ? realpath(path,NULL) : NULL;
            if(!bundle_path){error=n>=(int)sizeof(path) ? ENAMETOOLONG : errno;break;}
            if(strncmp(bundle_path,canonical_root,strlen(canonical_root)) || bundle_path[strlen(canonical_root)]!='/') {
                free(bundle_path);error=EPERM;break;
            }
            plist_t bundle=read_bundle(bundle_path);
            if(!bundle){free(bundle_path);error=EINVAL;break;}
            char *identifier=bundle_string(bundle,"CFBundleIdentifier");
            if(identifier && !strcmp(identifier,service)) {
                char *executable=bundle_string(bundle,"CFBundleExecutable");
                if(!executable || !*executable || strchr(executable,'/') ||
                   !strcmp(executable,".") || !strcmp(executable,".."))error=EINVAL;
                else {
                    char candidate[PATH_MAX];const char *slash=strrchr(bundle_path,'/');
                    n=snprintf(candidate,sizeof(candidate),"%.*s/%s",(int)(slash-bundle_path),bundle_path,executable);
                    char *resolved=n>0 && n<(int)sizeof(candidate) ? realpath(candidate,NULL) : NULL;
                    if(!resolved)error=n>=(int)sizeof(candidate) ? ENAMETOOLONG : errno;
                    else if(strncmp(resolved,canonical_root,strlen(canonical_root)) || resolved[strlen(canonical_root)]!='/') {
                        free(resolved);error=EPERM;
                    } else if(found) {
                        struct stat previous,current;
                        if(stat(found,&previous) || stat(resolved,&current))error=errno;
                        else if(previous.st_dev==current.st_dev && previous.st_ino==current.st_ino) {
                            /* Firmware framework aliases may expose one bundle
                             * through several paths. File identity, not glob
                             * order or a preferred framework name, resolves it. */
                            fprintf(stderr,"SESSION_SERVICE_BUNDLE_ALIAS service=%s first=%s alias=%s\n",service,found,resolved);
                            error=0;
                        } else {
                            fprintf(stderr,"SESSION_SERVICE_BUNDLE_CONFLICT service=%s first=%s other=%s\n",service,found,resolved);
                            error=EEXIST;
                        }
                        free(resolved);
                    } else {found=resolved;error=0;}
                }
                free(executable);
            } else error=EINVAL;
            free(identifier);plist_free(bundle);free(bundle_path);
            if(error && error!=ENOENT)break;
    }
    plist_free(catalog);free(canonical_root);
    if(error){free(found);return error;}
    *output=found;return 0;
}
static int child_start_ticks(pid_t pid,uint64_t *ticks)
{
    char path[64],buffer[4096];snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return errno;
    ssize_t n=read(fd,buffer,sizeof(buffer)-1);int error=errno;close(fd);
    if(n<=0)return n<0 ? error : EIO;
    buffer[n]=0;char *field=strrchr(buffer,')');if(!field)return EINVAL;
    field+=2;
    for(int i=0;i<19;i++){field=strchr(field,' ');if(!field)return EINVAL;field++;}
    char *end;*ticks=strtoull(field,&end,10);return end==field || !*ticks ? EINVAL : 0;
}
int native_service_metadata(uint32_t owner,uint32_t incarnation,const char *service,plist_t *configuration)
{
    struct caller_launch caller={0};int error=caller_launch_read(owner,&caller);
    if(error)return error;
    if(!darwin_native_host_process_matches(owner,incarnation))return ESRCH;
    char *program=NULL;error=service_program(caller.root,caller.service_index,service,&program);
    if(error)return error;
    char path[PATH_MAX];const char *slash=strrchr(program,'/');
    int n=snprintf(path,sizeof(path),"%.*s/Info.plist",(int)(slash-program),program);free(program);
    if(n<=0 || n>=(int)sizeof(path))return ENAMETOOLONG;
    plist_t info=read_bundle(path);if(!info)return EINVAL;
    plist_t value=plist_dict_get_item(info,"XPCService");
    if(!value || plist_get_node_type(value)!=PLIST_DICT)error=ENOTSUP;
    else {
        *configuration=plist_copy(value);
        if(!*configuration)error=ENOMEM;
    }
    plist_free(info);return error;
}
int native_service_spawn(uint32_t owner,uint32_t incarnation,const char *service,
                         const unsigned char instance[16],struct native_service_child *child)
{
    struct caller_launch caller={0};int error=caller_launch_read(owner,&caller);
    if(error)return error;
    if(!darwin_native_host_process_matches(owner,incarnation))return ESRCH;
    char *program=NULL;error=service_program(caller.root,caller.service_index,service,&program);
    if(error)return error;
    char executable[PATH_MAX];ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
    if(n<0){error=errno;free(program);return error;}executable[n]=0;
    char uuid[37];snprintf(uuid,sizeof(uuid),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        instance[0],instance[1],instance[2],instance[3],instance[4],instance[5],instance[6],instance[7],
        instance[8],instance[9],instance[10],instance[11],instance[12],instance[13],instance[14],instance[15]);
    char *args[24]={executable,"--role","service","--service-name",(char *)service,"--service-instance",uuid,
        "--root",(char *)caller.root,"--dyld",(char *)caller.dyld,"--container",(char *)caller.container,
        "--service-index",(char *)caller.service_index,NULL};
    size_t argument_count=0;while(args[argument_count])argument_count++;
    if(caller.group_root) {
        args[argument_count++]="--group-root";args[argument_count++]=(char *)caller.group_root;
    }
    args[argument_count++]=program;args[argument_count]=NULL;
    char prefix[PATH_MAX+64],server[64],provider[64];
    const char *directory=getenv("ARLINUX_DARLING_PREFIX");
    if(!directory || snprintf(prefix,sizeof(prefix),"ARLINUX_DARLING_PREFIX=%s",directory)>=(int)sizeof(prefix)) {
        free(program);return EINVAL;
    }
    snprintf(server,sizeof(server),"ARLINUX_DARLING_PID=%ld",(long)getpid());
    snprintf(provider,sizeof(provider),"ARLINUX_SESSION_PID=%ld",(long)getpid());
    char *environment[]={prefix,server,provider,NULL,NULL}; /* Never inherit host credentials. */
    const char *stop_on_fault=getenv("ARLINUX_STOP_ON_FAULT");
    if(stop_on_fault && !strcmp(stop_on_fault,"1"))environment[3]="ARLINUX_STOP_ON_FAULT=1";
    posix_spawnattr_t attributes;posix_spawn_file_actions_t actions;
    error=posix_spawnattr_init(&attributes);if(error){free(program);return error;}
    error=posix_spawn_file_actions_init(&actions);
    if(error){posix_spawnattr_destroy(&attributes);free(program);return error;}
    sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);sigaddset(&defaults,SIGPIPE);sigaddset(&defaults,SIGCHLD);
    if(!(error=posix_spawnattr_setsigmask(&attributes,&mask)))error=posix_spawnattr_setsigdefault(&attributes,&defaults);
    if(!error)error=posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF);
    if(!error)error=posix_spawn_file_actions_addclosefrom_np(&actions,3);
    if(!error && !darwin_native_host_process_matches(owner,incarnation))error=ESRCH;
    pid_t pid=0;
    if(!error)error=posix_spawn(&pid,executable,&actions,&attributes,args,environment);
    posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);free(program);
    if(error)return error;
    int pidfd=syscall(SYS_pidfd_open,pid,0);uint64_t ticks=0;
    error=pidfd<0 ? errno : child_start_ticks(pid,&ticks);
    if(error){kill(pid,SIGKILL);while(waitpid(pid,NULL,0)<0 && errno==EINTR){}if(pidfd>=0)close(pidfd);return error;}
    *child=(struct native_service_child){.pid=pid,.pidfd=pidfd,.start_ticks=ticks};return 0;
}
