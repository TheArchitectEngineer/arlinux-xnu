# Extract the production decoder, then exercise both register-state semantics
# and real Linux SIGILL delivery on ARM64 with independent per-thread TSD.
require 'tmpdir'
require 'open3'
root=File.realpath(ARGV.fetch(0, File.expand_path('..', __dir__)))
source=File.read("#{root}/darling/src/libsystem_kernel/emulation/src/linux_premigration/signal/sigexc.c")
helper=source[/static bool emulate_darling_tsd_read\(.*?^\}/m] or abort 'decoder missing'
Dir.mktmpdir('arm64-tsd-trap-') do |dir|
  File.write("#{dir}/test.cpp", <<~CPP)
    #include <stdint.h>
    #include <string.h>
    #include <assert.h>
    #include <signal.h>
    #include <ucontext.h>
    #include <pthread.h>
    #include <errno.h>
    #include <stdio.h>
    #include <stdlib.h>
    struct linux_gregset { uint64_t regs[31], sp, pc, pstate; };
    struct linux_ucontext { struct { linux_gregset gregs; } uc_mcontext; };
    static thread_local uint64_t guest_tsd[128];
    static void* sys_thread_get_tsd_base() { return guest_tsd; }
    #{helper}
    static void signal_handler(int number, siginfo_t *info, void *opaque) {
      assert(number==SIGILL && info->si_code>0);
      ucontext_t *native=(ucontext_t*)opaque;
      linux_ucontext context{};
      memcpy(context.uc_mcontext.gregs.regs,native->uc_mcontext.regs,sizeof(native->uc_mcontext.regs));
      context.uc_mcontext.gregs.pc=native->uc_mcontext.pc;
      context.uc_mcontext.gregs.sp=native->uc_mcontext.sp;
      context.uc_mcontext.gregs.pstate=native->uc_mcontext.pstate;
      assert(emulate_darling_tsd_read(&context));
      memcpy(native->uc_mcontext.regs,context.uc_mcontext.gregs.regs,sizeof(native->uc_mcontext.regs));
      native->uc_mcontext.pc=context.uc_mcontext.gregs.pc;
    }
    static void* worker(void *value) {
      guest_tsd[0]=(uintptr_t)value;
      void *native_tp=__builtin_thread_pointer();
      pthread_t self=pthread_self();
      for (unsigned i=0;i<1000;++i) {
        errno=E2BIG;
        uintptr_t result;
        asm volatile(".inst 0x0000da09\\nmov %0, x9" : "=r"(result) :: "x9", "memory");
        assert(result==(uintptr_t)guest_tsd && *(uint64_t*)result==(uintptr_t)value);
        assert(__builtin_thread_pointer()==native_tp && pthread_equal(pthread_self(),self));
        assert(errno==E2BIG);
      }
      return (void*)guest_tsd;
    }
    int main() {
      for (unsigned reg=0;reg<32;++reg) {
        uint32_t instruction=0xda00|reg;
        linux_ucontext context{};
        for (unsigned i=0;i<31;++i) context.uc_mcontext.gregs.regs[i]=0x1000+i;
        context.uc_mcontext.gregs.sp=0x20000;
        context.uc_mcontext.gregs.pstate=0xf0000000;
        context.uc_mcontext.gregs.pc=(uintptr_t)&instruction;
        assert(emulate_darling_tsd_read(&context));
        assert(context.uc_mcontext.gregs.pc==(uintptr_t)&instruction+4);
        assert(context.uc_mcontext.gregs.sp==0x20000 && context.uc_mcontext.gregs.pstate==0xf0000000);
        for (unsigned i=0;i<31;++i)
          assert(context.uc_mcontext.gregs.regs[i]==(i==reg ? (uintptr_t)guest_tsd : 0x1000+i));
      }
      for (uint32_t instruction : {0U,0xd9ffU,0xda20U,0xd53bd060U,0xd4200000U}) {
        linux_ucontext context{}; context.uc_mcontext.gregs.pc=(uintptr_t)&instruction;
        linux_ucontext before=context;
        assert(!emulate_darling_tsd_read(&context));
        assert(memcmp(&before,&context,sizeof(context))==0);
      }
      struct sigaction action{}; action.sa_sigaction=signal_handler; action.sa_flags=SA_SIGINFO;
      sigemptyset(&action.sa_mask); assert(sigaction(SIGILL,&action,nullptr)==0);
      pthread_t threads[4]; void* bases[4];
      for (uintptr_t i=0;i<4;++i) assert(pthread_create(&threads[i],nullptr,worker,(void*)(i+1))==0);
      for (unsigned i=0;i<4;++i) assert(pthread_join(threads[i],&bases[i])==0);
      for (unsigned i=0;i<4;++i) for (unsigned j=i+1;j<4;++j) assert(bases[i]!=bases[j]);
      puts("PASS all destinations/XZR, unrelated instruction rejection, preserved PC/SP/flags, and 4000 concurrent SIGILL reads without native TLS corruption");
    }
  CPP
  # Needed for the range-for list on older libc++/libstdc++ setups.
  path="#{dir}/test.cpp"
  File.write(path,"#include <initializer_list>\n"+File.read(path))
  out,status=Open3.capture2e('clang++','-std=c++11','-pthread','-fsanitize=undefined',path,'-o',"#{dir}/test")
  abort out unless status.success?
  out,status=Open3.capture2e("#{dir}/test")
  puts out
  abort 'TSD trap regression failed' unless status.success?
end
