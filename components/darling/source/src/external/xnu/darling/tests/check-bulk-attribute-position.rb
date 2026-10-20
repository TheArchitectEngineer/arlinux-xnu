# Actual bulk-wrapper control flow with real Linux directory syscalls.
# Attribute packing is controlled: inject one lookup EACCES and make its error
# record report ERANGE, so that entry must remain available for retry.
require 'tmpdir'
require 'open3'
root=File.realpath(ARGV.fetch(0))
path='darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/xattr/getattrlistbulk.c'
source=File.read(File.join(root,path))
if ENV['BULK_SOURCE_REF']
  source,status=Open3.capture2('git','-C',root,'show',"#{ENV.fetch('BULK_SOURCE_REF')}:#{path}")
  abort 'source revision lookup failed' unless status.success?
end
body=source[source.index('#define ATTR_CMN_NAME')..]
abort 'bulk implementation missing' unless body
program=<<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <stdint.h>
  #include <stddef.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <sys/syscall.h>
  #include <unistd.h>
  struct xnu_attrlist { uint16_t bitmapcount,reserved; uint32_t commonattr,volattr,dirattr,fileattr,forkattr; };
  struct linux_dirent64 { uint64_t d_ino; int64_t d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };
  static int shortFirstBatch, batches;
  static int malformed;
  static int seekFailure, rewinds;
  static int classificationFails, classifications, errorPacks;
  static long probe_getdents(int fd,void *buffer,size_t size) {
    if(malformed) {
      memset(buffer,'x',size);
      struct linux_dirent64 *entry=buffer;
      entry->d_reclen=24;
      if(malformed==1) return 1;
      if(malformed==2) entry->d_reclen=0;
      if(malformed==3) entry->d_reclen=16;
      if(malformed==4) entry->d_reclen=32;
      if(malformed==5) entry->d_reclen=23;
      if(malformed==6) return (long)size+1;
      if(malformed==8) entry->d_name[0]=0;
      return 24; // case 7 has no NUL within its record
    }
    if(shortFirstBatch && batches++==0) {
      // Controlled skipped-only batch; subsequent batches use the real fd.
      assert(size>=48); memset(buffer,0,48);
      for(unsigned i=0;i<2;++i) {
        struct linux_dirent64 *entry=(void *)((char *)buffer+24*i);
        entry->d_reclen=24; entry->d_off=0;
        strcpy(entry->d_name,i==0 ? "." : "..");
      }
      return 48;
    }
    long result=syscall(__NR_getdents64,fd,buffer,size);
    if(result>0 && classificationFails) {
      for(char *p=buffer;p<(char *)buffer+result;) {
        struct linux_dirent64 *entry=(void *)p;
        entry->d_type=0; p+=entry->d_reclen;
      }
    }
    return result;
  }
  static long probe_seek(int fd,long offset,int whence) {
    if(seekFailure && whence==SEEK_SET) { ++rewinds; errno=ESPIPE; return -1; }
    return syscall(__NR_lseek,fd,offset,whence);
  }
  #define LINUX_SYSCALL(number,fd,arg,last) ({ \
    long r=(number)==__NR_getdents64 ? probe_getdents(fd,(void *)(uintptr_t)(arg),last) : probe_seek(fd,(long)(arg),last); \
    r<0 ? -errno : r; })
  static long errno_linux_to_bsd(long value) { return value; }
  static int darling_attribute_is_directory(int fd,const char *name) {
    (void)fd; (void)name; ++classifications;
    return classificationFails ? -ENOENT : 0;
  }
  static long darling_pack_attribute_error(const char *name,int directory,
      struct xnu_attrlist *list,void *buffer,size_t available,unsigned long options,uint32_t error) {
    (void)name; (void)directory; (void)list; (void)buffer; (void)available; (void)options;
    assert(error==EACCES);
    ++errorPacks;
    return -ERANGE; // Controlled failure to fit the error record.
  }
  static unsigned attempts, emitted, failAt;
  static uint32_t forcedLength;
  static char names[3][16];
  static long sys_getattrlistat(int fd,const char *name,struct xnu_attrlist *list,
      void *output,size_t available,unsigned long options) {
    (void)fd; (void)list; (void)options;
    if(seekFailure==2 && attempts==1) { ++attempts; return -ERANGE; }
    if(++attempts==failAt) return -EACCES;
    assert(available>=32 && emitted<3 && strlen(name)<sizeof(names[0]));
    for(unsigned i=0;i<emitted;++i) assert(strcmp(names[i],name)!=0);
    strcpy(names[emitted++],name);
    memset(output,0,32); uint32_t length=forcedLength ? forcedLength : 32; memcpy(output,&length,4);
    return 0;
  }
  #{body}
  int main(int argc,char **argv) {
    assert(argc==4 || argc==5); failAt=(unsigned)atoi(argv[1]); shortFirstBatch=atoi(argv[2]);
    classificationFails=atoi(argv[3]);
    assert(failAt>=1 && failAt<=3);
    char directory[]="/tmp/bulk-position-XXXXXX";
    assert(mkdtemp(directory));
    int fd=open(directory,O_RDONLY|O_DIRECTORY); assert(fd>=0);
    const char *files[]={"a","b","c"};
    for(unsigned i=0;i<3;++i) { int f=openat(fd,files[i],O_CREAT|O_WRONLY,0600); assert(f>=0); close(f); }
    struct xnu_attrlist list={.bitmapcount=5,.commonattr=0x80000001};
    unsigned char storage[1025];
    memset(storage,0xa5,sizeof(storage));
    void *buffer=storage+1; // User buffers need not have uint32_t alignment.
    unsigned long options=classificationFails ? 8 : 0;
    if(argc==5) {
      if(strncmp(argv[4],"seek",4)==0) {
        seekFailure=atoi(argv[4]+4); failAt=seekFailure==3 ? 2 : 0;
        size_t capacity=seekFailure==1 || seekFailure==4 ? 32 : 1024;
        if(seekFailure==4) forcedLength=33;
        assert(sys_getattrlistbulk(fd,&list,buffer,capacity,options)==-ESPIPE);
        assert(rewinds==1);
        for(unsigned i=0;i<3;++i) assert(unlinkat(fd,files[i],0)==0);
        close(fd); assert(rmdir(directory)==0);
        puts("PASS: failed directory rewind overrides partial success");
        return 0;
      }
      if(strncmp(argv[4],"bad",3)==0) {
        malformed=atoi(argv[4]+3);
        assert(sys_getattrlistbulk(fd,&list,buffer,1024,options)==-EIO);
        assert(attempts==0);
        for(unsigned i=0;i<sizeof(storage);++i) assert(storage[i]==0xa5);
        for(unsigned i=0;i<3;++i) assert(unlinkat(fd,files[i],0)==0);
        close(fd); assert(rmdir(directory)==0);
        puts("PASS: malformed directory batch rejected before attribute lookup");
        return 0;
      }
      forcedLength=(uint32_t)strtoul(argv[4],NULL,0); failAt=0;
      long result=sys_getattrlistbulk(fd,&list,buffer,1024,options);
      assert(result==-EIO && emitted==1);
      assert(storage[0]==0xa5 && storage[1024]==0xa5);
      for(unsigned i=0;i<3;++i) assert(unlinkat(fd,files[i],0)==0);
      close(fd); assert(rmdir(directory)==0);
      puts("PASS: invalid packed length rejected before alignment rounding");
      return 0;
    }
    long first=sys_getattrlistbulk(fd,&list,buffer,1024,options);
    if(failAt==1) for(unsigned i=0;i<sizeof(storage);++i) assert(storage[i]==0xa5);
    long second=sys_getattrlistbulk(fd,&list,buffer,1024,options);
    long last=sys_getattrlistbulk(fd,&list,buffer,1024,options);
    assert(classifications==(classificationFails ? 1 : 0));
    assert(errorPacks==(classificationFails ? 0 : 1));
    printf("shortBatch=%d classificationFailure=%d failAt=%u first=%ld retry=%ld final=%ld emitted=%u\\n",shortFirstBatch,classificationFails,failAt,first,second,last,emitted);
    for(unsigned i=0;i<3;++i) assert(unlinkat(fd,files[i],0)==0);
    close(fd); assert(rmdir(directory)==0);
    long expectedFirst=failAt==1 ? (classificationFails ? -EACCES : -ERANGE) : (long)failAt-1;
    if(first!=expectedFirst || second!=4-(long)failAt || last!=0 || emitted!=3) return 1;
    puts("PASS: per-entry failure preserves the unprocessed directory position");
  }
C
Dir.mktmpdir('bulk-position-build-') do |dir|
  input=File.join(dir,'probe.c'); binary=File.join(dir,'probe')
  File.write(input,program)
  abort 'compile failed' unless system('clang','-std=gnu11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined',input,'-o',binary)
  (1..3).each do |failure|
    [0,1].each do |short_batch|
      [0,1].each do |classification_failure|
        abort 'bulk position/alignment regression' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},binary,failure.to_s,short_batch.to_s,classification_failure.to_s)
      end
    end
  end
  ['1', '3', '0xfffffff9', '0xffffffff'].each do |length|
    abort 'bulk record-length regression' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},binary,'1','0','0',length)
  end
  (1..8).each do |kind|
    abort 'bulk directory-record bounds regression' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},binary,'1','0','0',"bad#{kind}")
  end
  (1..4).each do |kind|
    abort 'bulk rewind failure regression' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},binary,'1','0','0',"seek#{kind}")
  end
end
