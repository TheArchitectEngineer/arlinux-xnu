/* Native memory-pressure source, independent of host libc/TLS. This uses
 * Linux physical MemAvailable, not an emulated iPhone/Jetsam limit. No
 * per-process Jetsam cap is installed by this standalone adapter. */
#pragma once
static long native_meminfo_number(const char *buffer,unsigned long length,
                                  const char *key,unsigned int key_length,uint64_t *value)
{
    for (unsigned long i=0;i+key_length<length;i++) {
        if (i && buffer[i-1]!='\n') continue;
        unsigned int matched=0;
        while (matched<key_length && buffer[i+matched]==key[matched]) matched++;
        if (matched!=key_length) continue;
        i+=key_length;
        while (i<length && (buffer[i]==' ' || buffer[i]=='\t')) i++;
        uint64_t number=0; unsigned int digits=0;
        while (i<length && buffer[i]>='0' && buffer[i]<='9') {
            unsigned int digit=buffer[i++]-'0';
            if (number>(UINT64_MAX-digit)/10) return -EOVERFLOW;
            number=number*10+digit; digits++;
        }
        if (!digits) return -EIO;
        *value=number; return 0;
    }
    return -EIO;
}
static long native_memory_pressure(void)
{
    long fd=RAW(SYS_openat,AT_FDCWD,"/proc/meminfo",O_RDONLY|O_CLOEXEC,0,0,0);
    if (fd<0) return fd;
    char buffer[4096];
    long size=RAW(SYS_read,fd,buffer,sizeof(buffer),0,0,0);
    RAW(SYS_close,fd,0,0,0,0,0);
    if (size<=0) return size<0 ? size : -EIO;
    uint64_t total,available;
    long error=native_meminfo_number(buffer,size,"MemTotal:",9,&total);
    if (!error) error=native_meminfo_number(buffer,size,"MemAvailable:",13,&available);
    if (error) return error;
    if (!total || available>total || total>UINT64_MAX/100) return -EIO;
    return available*100<=total*D_MEMORY_CRITICAL_PERCENT ? D_MEMORY_CRITICAL :
           available*100<=total*D_MEMORY_WARN_PERCENT ? D_MEMORY_WARN : D_MEMORY_NORMAL;
}
