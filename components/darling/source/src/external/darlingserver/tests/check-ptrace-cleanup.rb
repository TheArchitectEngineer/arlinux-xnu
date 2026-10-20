# Exercise the actual thread-discovery loop with controlled ptrace failures.
require 'tmpdir'
require 'open3'
root=File.realpath(ARGV.fetch(0))
source=File.read("#{root}/src/thread.cpp")
loop_source=source[/\t+for \(auto id : ids\) \{.*?(?=\n\s*_tid = chosenId;)/m]
abort 'discovery loop missing' unless loop_source
Dir.mktmpdir('server-ptrace-cleanup-') do |dir|
  File.write("#{dir}/probe.cpp", <<~CPP)
    #include <cassert>
    #include <cerrno>
    #include <cstdint>
    #include <cstdio>
    #include <limits>
    #include <system_error>
    #include <vector>
    #include <sys/uio.h>
    enum { PTRACE_ATTACH, PTRACE_DETACH, PTRACE_GETREGS, PTRACE_GETREGSET, NT_PRSTATUS };
    struct user_regs_struct { uintptr_t sp,rsp; };
    static int detach_count,fail_registers,wait_calls,stopped;
    static int waitpid(int id,int *status,int flags) {
      if (#{ARGV[1]=='--interrupted-wait' ? 'true' : 'false'} && wait_calls++==0) { errno=EINTR; return -1; }
      stopped=1; *status=0; return id;
    }
    static long ptrace(int request,int id,void *address,void *data) {
      if(request==PTRACE_ATTACH) return 0;
      if(request==PTRACE_DETACH) {
        if(!stopped) { errno=ESRCH; return -1; }
        detach_count++; return 0;
      }
      if(fail_registers) { errno=EIO; return -1; }
      auto *regs=request==PTRACE_GETREGSET ?
          static_cast<user_regs_struct *>(static_cast<iovec *>(data)->iov_base) :
          static_cast<user_regs_struct *>(data);
      regs->sp=regs->rsp=0x1000; return 0;
    }
    int main() {
      for(int failing=0;failing<2;failing++) {
        fail_registers=failing; detach_count=wait_calls=stopped=0;
        std::vector<int> ids={123}; int chosenId=-1;
        intptr_t nearest=std::numeric_limits<intptr_t>::max();
        void *stackHint=reinterpret_cast<void *>(0x1100);
        #{loop_source}
        std::printf("register_failure=%d detach=%d chosen=%d\\n",failing,detach_count,chosenId);
        assert(detach_count==1);
        assert(chosenId==(failing?-1:123));
      }
    }
  CPP
  out,status=Open3.capture2e('clang++','-std=c++17','-O1','-fsanitize=address,undefined',"#{dir}/probe.cpp",'-o',"#{dir}/probe")
  abort out unless status.success?
  exit(system("#{dir}/probe",rlimit_core:0) ? 0 : 1)
end
