/* Adapt the genuine client's launchd-only trust predicate to our in-task
 * bootstrap provider. Evidence: run 39387154773, real audit getter followed
 * by PID/euid checks. No firmware address or release version is assumed. */
#pragma once
static unsigned int cache_bootstrap_policy_hooks;
static uint64_t cache_cb_target(uint64_t pc,uint32_t word)
{
    int32_t offset=(int32_t)(word<<8)>>13;
    return pc+(int64_t)offset*4;
}
static void cache_bootstrap_policy(uint64_t base,uint64_t bytes)
{
    uint32_t *words=(void *)base;
    for (uint64_t i=3;i+8<bytes/4;i++) {
        uint32_t *p=words+i; uint64_t pc=base+i*4;
        if (p[-3]!=0x910043e1U || p[-2]!=0xaa1503e0U ||
            (p[-1]&0xfc000000U)!=0x94000000U ||
            p[0]!=0xb94027e8U || p[1]!=0x7100051fU ||
            (p[2]&0xff00001fU)!=0x54000001U ||
            (p[3]&0xff00001fU)!=0x35000014U ||
            (p[4]&0xfc000000U)!=0x14000000U || p[5]!=0xb94017e8U ||
            (p[6]&0xff00001fU)!=0x35000008U ||
            (p[7]&0xff00001fU)!=0x34000014U) continue;
        uint64_t reject=cache_cb_target(pc+24,p[6]);
        if (cache_cb_target(pc+8,p[2])!=pc+20 ||
            cache_cb_target(pc+12,p[3])!=pc+32 ||
            cache_cb_target(pc+28,p[7])!=reject || !cache_contains(reject,8,4)) continue;
        uint32_t old; memcpy(&old,(void *)reject,4);
        if ((old&0xff00001fU)!=0x34000008U) continue;
        int64_t delta=(int64_t)(reject+4)-(int64_t)(pc+12);
        need(!(delta&3) && delta>=-(1<<20) && delta<(1<<20),"bootstrap rejection branch range");
        /* add x0,sp,#16; ldr x16,literal; blr x16; cbnz w0,reject;
         * b original requester-echo checks; nop; .quad policy helper.
         * The original frame owns LR; the helper preserves callee-saved GPRs
         * and uses no FP instructions or guest-TLS-dependent host libc. */
        uint32_t code[6]={0x910043e0U,0x580000b0U,0xd63f0200U,
            0x35000000U|(((uint32_t)(delta/4)&0x7ffffU)<<5),0x14000004U,0xd503201fU};
        uint64_t helper=(uintptr_t)darwin_native_bootstrap_peer;
        memcpy(p,code,sizeof(code)); memcpy(p+6,&helper,sizeof(helper));
        cache_bootstrap_policy_hooks++;
        fprintf(stderr,"NATIVE_BOOTSTRAP_POLICY address=%#" PRIx64 " peer=same-task-pid-uid-incarnation\n",pc);
    }
}
