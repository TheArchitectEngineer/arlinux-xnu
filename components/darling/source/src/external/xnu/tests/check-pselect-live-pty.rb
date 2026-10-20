# Native Linux PTY test of the actual merged wrapper with direct syscall adapters.
# No Darwin guest or Darwin-to-Linux signal-number conversion is exercised.
require 'tmpdir'
require 'open3'
root=File.realpath(ARGV.fetch(0))
source=File.read("#{root}/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/select/pselect.c").lines.reject{|l|l.start_with?('#include ')}.join
Dir.mktmpdir('pselect-live-pty-') do |dir|
  File.write("#{dir}/probe.c", <<~C)
    #define _GNU_SOURCE
    #include <assert.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <dirent.h>
    #include <pthread.h>
    #include <signal.h>
    #include <pty.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>
    #include <sys/ioctl.h>
    #include <sys/syscall.h>
    #include <time.h>
    #include <unistd.h>
    typedef sigset_t host_sigset_t;
    typedef unsigned bsd_sigset_t;
    #define sigset_t bsd_sigset_t
    typedef unsigned long long linux_sigset_t;
    struct bsd_timeval { long tv_sec; int tv_usec; };
    #define CANCELATION_POINT() ((void)0)
    static int fail_mmap,live_mappings;
    #define LINUX_SYSCALL(n, ...) ({ \\
      long result; \\
      if ((n)==__NR_mmap && fail_mmap) result=-ENOMEM; \\
      else { result=syscall(n,__VA_ARGS__); if(result<0) result=-errno; \\
        else if((n)==__NR_mmap) live_mappings++; \\
        else if((n)==__NR_munmap) live_mappings--; } \\
      result; })
    static int errno_linux_to_bsd(int e) { return e; }
    static void sigset_bsd_to_linux(const sigset_t *in, linux_sigset_t *out) { *out=*in; }
    static int __real_ioctl(int fd,int request,void *data) { return ioctl(fd,request,data); }
    long sys_pselect_nocancel(int,void*,void*,void*,struct bsd_timeval*,const sigset_t*);
    #{source}
    static void *close_later(void *p) { usleep(100000); close(*(int *)p); return NULL; }
    static volatile sig_atomic_t delivered;
    static void caught(int signal) { delivered++; }
    static void *signal_later(void *p) { usleep(100000); pthread_kill(*(pthread_t *)p,SIGUSR1); return NULL; }
    static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
    static int open_fds(void) {
      DIR *dir=opendir("/proc/self/fd"); assert(dir);
      int count=0; struct dirent *entry;
      while((entry=readdir(dir))) if(entry->d_name[0]!='.') count++;
      closedir(dir); return count;
    }
    int main(void) {
      int failures=0;
      for(int delayed=0;delayed<2;delayed++) {
        int master,slave; assert(openpty(&master,&slave,NULL,NULL,NULL)==0);
        assert(master<1024);
        uint32_t exceptions[32]={0}; exceptions[master/32]|=1u<<(master%32);
        pthread_t closer;
        if(delayed) assert(pthread_create(&closer,NULL,close_later,&slave)==0);
        else close(slave);
        struct bsd_timeval timeout={0,600000};
        double start=now();
        long result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&timeout,NULL);
        double elapsed=now()-start;
        if(delayed) pthread_join(closer,NULL);
        int ready=!!(exceptions[master/32]&(1u<<(master%32)));
        printf("%s hangup: result=%ld exception=%d elapsed=%.3fs\\n",delayed?"delayed":"preexisting",result,ready,elapsed);
        if(result!=1 || !ready || elapsed>0.45) failures++;
        close(master);
      }
      /* A future ppoll-based path must not expose unrelated pipe HUP as
         exception readiness or return early merely because poll sees it. */
      int pipefd[2]; assert(pipe(pipefd)==0); close(pipefd[1]);
      uint32_t exceptions[32]={0};
      exceptions[pipefd[0]/32]|=1u<<(pipefd[0]%32);
      struct bsd_timeval short_timeout={0,100000};
      double start=now();
      long result=sys_pselect_nocancel(pipefd[0]+1,NULL,NULL,exceptions,&short_timeout,NULL);
      double elapsed=now()-start;
      printf("non-PTY hangup: result=%ld elapsed=%.3fs\\n",result,elapsed);
      if(result!=0 || exceptions[pipefd[0]/32]!=0 || elapsed<0.08) failures++;
      close(pipefd[0]);
      /* Ordinary readable data must not become a PTY exception. */
      int master,slave; assert(openpty(&master,&slave,NULL,NULL,NULL)==0);
      assert(write(slave,"x",1)==1);
      memset(exceptions,0,sizeof(exceptions));
      exceptions[master/32]|=1u<<(master%32);
      short_timeout=(struct bsd_timeval){0,100000}; start=now();
      result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&short_timeout,NULL);
      elapsed=now()-start;
      printf("PTY ordinary data: result=%ld elapsed=%.3fs\\n",result,elapsed);
      if(result!=0 || exceptions[master/32]!=0 || elapsed<0.08) failures++;
      /* A descriptor ready in two requested sets contributes two bits. */
      uint32_t reads[32]={0},writes[32]={0};
      reads[master/32]|=1u<<(master%32); writes[master/32]|=1u<<(master%32);
      short_timeout=(struct bsd_timeval){0,100000};
      memset(exceptions,0,sizeof(exceptions));
      exceptions[master/32]|=1u<<(master%32);
      result=sys_pselect_nocancel(master+1,reads,writes,exceptions,&short_timeout,NULL);
      printf("PTY read/write readiness: result=%ld\\n",result);
      if(result!=2 || !(reads[master/32]&(1u<<(master%32))) ||
          !(writes[master/32]&(1u<<(master%32)))) failures++;
      /* Include a live PTY to force the candidate ppoll path while an
         unrelated pipe has an unrequested terminal event. */
      assert(pipe(pipefd)==0); close(pipefd[1]);
      int limit=(master>pipefd[0]?master:pipefd[0])+1;
      memset(exceptions,0,sizeof(exceptions));
      exceptions[master/32]|=1u<<(master%32);
      exceptions[pipefd[0]/32]|=1u<<(pipefd[0]%32);
      short_timeout=(struct bsd_timeval){0,100000}; start=now();
      result=sys_pselect_nocancel(limit,NULL,NULL,exceptions,&short_timeout,NULL);
      elapsed=now()-start;
      printf("mixed ignored pipe hangup: result=%ld elapsed=%.3fs\\n",result,elapsed);
      if(result!=0 || elapsed<0.08 || elapsed>0.45) failures++;
      /* An ignored pipe HUP must not hide a subsequent PTY HUP. */
      memset(exceptions,0,sizeof(exceptions));
      exceptions[master/32]|=1u<<(master%32);
      exceptions[pipefd[0]/32]|=1u<<(pipefd[0]%32);
      pthread_t closer; assert(pthread_create(&closer,NULL,close_later,&slave)==0);
      short_timeout=(struct bsd_timeval){0,600000}; start=now();
      result=sys_pselect_nocancel(limit,NULL,NULL,exceptions,&short_timeout,NULL);
      elapsed=now()-start; pthread_join(closer,NULL);
      printf("mixed delayed PTY hangup: result=%ld elapsed=%.3fs\\n",result,elapsed);
      if(result!=1 || elapsed>0.45 || !(exceptions[master/32]&(1u<<(master%32))) ||
          (exceptions[pipefd[0]/32]&(1u<<(pipefd[0]%32)))) failures++;
      close(pipefd[0]);
      close(master);
      /* The wait must temporarily unblock a signal, return EINTR, then restore
         the original blocked mask. Conversion is an identity test adapter. */
      assert(openpty(&master,&slave,NULL,NULL,NULL)==0);
      struct sigaction action={0}; action.sa_handler=caught;
      sigemptyset(&action.sa_mask); assert(sigaction(SIGUSR1,&action,NULL)==0);
      host_sigset_t blocked,original,after;
      sigemptyset(&blocked); sigaddset(&blocked,SIGUSR1);
      assert(pthread_sigmask(SIG_BLOCK,&blocked,&original)==0);
      pthread_t self=pthread_self(),sender;
      assert(pthread_create(&sender,NULL,signal_later,&self)==0);
      memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
      short_timeout=(struct bsd_timeval){0,600000};
      sigset_t temporary_mask=0; start=now();
      result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&short_timeout,&temporary_mask);
      elapsed=now()-start; pthread_join(sender,NULL);
      assert(pthread_sigmask(SIG_SETMASK,NULL,&after)==0);
      printf("signal interruption: result=%ld delivered=%d restored=%d elapsed=%.3fs\\n",
          result,delivered,sigismember(&after,SIGUSR1),elapsed);
      if(result!=-EINTR || delivered!=1 || !sigismember(&after,SIGUSR1) || elapsed>0.45) failures++;
      assert(pthread_sigmask(SIG_SETMASK,&original,NULL)==0);
      assert(pipe(pipefd)==0);
      int invalid=pipefd[0]; close(pipefd[0]); close(pipefd[1]);
      memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
      memset(reads,0,sizeof(reads)); reads[invalid/32]|=1u<<(invalid%32);
      limit=(master>invalid?master:invalid)+1;
      short_timeout=(struct bsd_timeval){0,100000};
      result=sys_pselect_nocancel(limit,reads,NULL,exceptions,&short_timeout,NULL);
      printf("invalid mixed descriptor: result=%ld\\n",result);
      if(result!=-EBADF) failures++;
      memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
      short_timeout=(struct bsd_timeval){-1,0};
      result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&short_timeout,NULL);
      printf("negative timeout: result=%ld\\n",result);
      if(result!=-EINVAL) failures++;
      int before=open_fds();
      for(int i=0;i<100;i++) {
        memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
        /* Set every padding bit above the caller's nfds in the last word. */
        int nfds=master+1;
        if(nfds%32) exceptions[nfds/32]|=~((1u<<(nfds%32))-1);
        short_timeout=(struct bsd_timeval){0,0};
        result=sys_pselect_nocancel(nfds,NULL,NULL,exceptions,&short_timeout,NULL);
        if(result!=0) failures++;
        memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
        short_timeout=(struct bsd_timeval){-1,0};
        result=sys_pselect_nocancel(nfds,NULL,NULL,exceptions,&short_timeout,NULL);
        if(result!=-EINVAL) failures++;
      }
      int after_count=open_fds();
      printf("repeated timeout/error and padding: fd count %d -> %d\\n",before,after_count);
      if(before!=after_count) failures++;
      int held[1200],held_count=0,highest=-1;
      while(highest<1100 && held_count<1200) {
        highest=open("/dev/null",O_RDONLY); assert(highest>=0);
        held[held_count++]=highest;
      }
      before=open_fds();
      memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
      short_timeout=(struct bsd_timeval){0,0};
      result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&short_timeout,NULL);
      after_count=open_fds();
      printf("high internal fd: highest=%d result=%ld fd count %d -> %d\\n",highest,result,before,after_count);
      if(result!=0 || before!=after_count) failures++;
      if(live_mappings!=0) failures++;
      memset(exceptions,0,sizeof(exceptions)); exceptions[master/32]|=1u<<(master%32);
      uint32_t saved[32]; memcpy(saved,exceptions,sizeof(saved));
      fail_mmap=1;
      result=sys_pselect_nocancel(master+1,NULL,NULL,exceptions,&short_timeout,NULL);
      fail_mmap=0; after_count=open_fds();
      printf("injected mmap failure: result=%ld fd count=%d mappings=%d\\n",result,after_count,live_mappings);
      if(result!=-ENOMEM || before!=after_count || live_mappings!=0 || memcmp(saved,exceptions,sizeof(saved))) failures++;
      for(int i=0;i<held_count;i++) close(held[i]);
      close(master); close(slave);
      return failures ? 1 : 0;
    }
  C
  out,status=Open3.capture2e('clang','-O2','-pthread',"#{dir}/probe.c",'-lutil','-o',"#{dir}/probe")
  abort out unless status.success?
  exit(system("#{dir}/probe") ? 0 : 1)
end
