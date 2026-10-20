# Tests actual conversion bodies and Darwin's public NEON state declaration.
# Optional root permits testing the upstream baseline without editing it.
require 'tmpdir'
root = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
base = "#{root}/darling/src/libsystem_kernel/emulation"
source = File.read("#{base}/src/linux_premigration/signal/sigexc.c")
header = File.read("#{base}/include/conversion/signal/sigaction.h")
darwin = File.read("#{root}/osfmk/mach/arm/_structs.h")
wire = darwin[/_STRUCT_ARM_NEON_STATE64\n\{\n\s*__uint128_t q\[32\];.*?\n};/m] or abort 'missing Darwin state'
wire = wire.sub('_STRUCT_ARM_NEON_STATE64', 'struct darwin_neon_state')
state = source[/typedef struct \{[^\n]+\} arm_neon_state64_t_linux;/] or abort 'missing RPC state'
structures = %w[linux_aarch64_ctx linux_fpsimd_context].map do |name|
  header[/struct #{name} \{.*?\n};/m] or abort "missing #{name}"
end.join("\n")
functions = %w[mcontext_to_float_state float_state_to_mcontext].map do |name|
  source.scan(/^void #{name}\([^\n]*\n\{.*?\n}/m).find { |body| body.lines.first.include?('arm_neon_state64_t_linux') } or abort "missing #{name}"
end.join("\n")
program = <<~C
  #include <assert.h>
  #include <stddef.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <string.h>
  #{wire}
  #{state}
  #{structures}
  #define FPSIMD_MAGIC 0x46508001
  #define LINUX_MCONTEXT_RESERVED_SIZE 4096
  #{functions}
  _Static_assert(sizeof(arm_neon_state64_t_linux)==sizeof(struct darwin_neon_state), "wire size");
  #define OFFSET(a,b) _Static_assert(offsetof(arm_neon_state64_t_linux,a)==offsetof(struct darwin_neon_state,b), "wire offset " #a)
  OFFSET(vregs,q); OFFSET(fpsr,fpsr); OFFSET(fpcr,fpcr);
  int main(void) {
    _Alignas(16) unsigned char area[4096] = {0};
    struct linux_fpsimd_context *input = (void*)area;
    input->head.magic = FPSIMD_MAGIC; input->head.size = sizeof(*input);
    input->fpsr = 0x12; input->fpcr = 0x34;
    for (int i=0; i<32; ++i) input->vregs[i] = ((__uint128_t)(i+1)<<80) | (i+200);
    arm_neon_state64_t_linux rpc;
    mcontext_to_float_state(area, &rpc);
    struct darwin_neon_state server;
    memcpy(&server, &rpc, sizeof(server));
    assert(server.fpsr==0x12 && server.fpcr==0x34);
    assert(memcmp(server.q,input->vregs,sizeof(server.q))==0);
    server.fpsr=0x56; server.fpcr=0x78;
    for (int i=0; i<32; ++i) server.q[i] ^= (__uint128_t)0xabc<<96;
    memcpy(&rpc,&server,sizeof(rpc));
    float_state_to_mcontext(&rpc,area);
    assert(input->fpsr==0x56 && input->fpcr==0x78);
    assert(memcmp(input->vregs,server.q,sizeof(server.q))==0);
    memset(area,0,sizeof(area)); memset(&rpc,0xff,sizeof(rpc));
    mcontext_to_float_state(area,&rpc);
    arm_neon_state64_t_linux zero={0};
    assert(memcmp(&rpc,&zero,sizeof(rpc))==0);
    puts("PASS: Darwin wire offsets and 32-vector/control-register transfer in both directions");
  }
C
Dir.mktmpdir('neon-rpc-layout-') do |dir|
  File.write("#{dir}/probe.c",program)
  %w[-O0 -O2].each do |optimization|
    abort 'compile failed' unless system(ENV.fetch('CC','clang'), optimization,
      '-fsanitize=address,undefined', "#{dir}/probe.c", '-o', "#{dir}/probe")
    abort 'test failed' unless system("#{dir}/probe", rlimit_core: 0)
  end
end
