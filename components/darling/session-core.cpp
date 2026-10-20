/* ARLinux session embedding of the GPLv3 Darling Mach core.
 * The caller owns this persistent host process; apps remain separate processes.
 * No Darling desktop launcher, namespace setup or second service is started. */
#include "session-core.h"
#include <darlingserver/server.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <signal.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

extern "C" int arlinux_session_core_start(const char *prefix)
{
    struct stat info;
    if (!prefix || std::strlen(prefix)+sizeof("/.darlingserver.sock")>sizeof(sockaddr_un::sun_path))
        return -EINVAL;
    if (stat(prefix,&info)) return -errno;
    if (!S_ISDIR(info.st_mode) || info.st_uid!=getuid() || (info.st_mode&077)) return -EACCES;
    if (signal(SIGPIPE,SIG_IGN)==SIG_ERR) return -errno;
    try {
        auto *core=new DarlingServer::Server(prefix);
        std::thread([core]() {
            try { core->start(); }
            catch (const std::exception& error) {
                std::fprintf(stderr,"SESSION_MACH_CORE_FAILED %s\n",error.what());
            }
            /* A dead IPC core invalidates the whole session. Do not leave an
             * apparently ready bootstrap process accepting dead capabilities. */
            _exit(70);
        }).detach();
        std::fprintf(stderr,"SESSION_MACH_CORE_STARTED pid=%ld prefix=%s\n",(long)getpid(),prefix);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"SESSION_MACH_CORE_FAILED %s\n",error.what());
        return -EIO;
    }
}
