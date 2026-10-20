/* Adapt the actual libxpc launchd predicate to our independent session peer.
 * Original audit/branch evidence: run 39463796818. No firmware address, image
 * version, fake PID 1 or fabricated audit credential selects this adapter.
 * Only the PID/euid prefix changes; requester-echo checks remain original.
 */
#pragma once
#include <stddef.h>
#include "darwin-abi.h"
static unsigned int cache_bootstrap_policy_hooks;
static uint64_t cache_cb_target(uint64_t pc,uint32_t word)
{
    int32_t offset=(int32_t)(word<<8)>>13;
    return pc+(int64_t)offset*4;
}
static int cache_policy_instruction(uint64_t address,uint64_t base,uint64_t size,uint32_t *word)
{
    if(address<base || !range(address-base,4,size))return 0;
    memcpy(word,(void *)address,4);return 1;
}
static void cache_bootstrap_policy(uint64_t base,uint64_t bytes)
{
    uint32_t *words=(void *)base;
    for(uint64_t i=3;i+10<bytes/4;i++) {
        uint32_t *p=words+i;uint64_t pc=base+i*4;
        /* add x1,sp,#token; mov x0,object; bl audit_getter;
         * ldr w0,[sp,#pid]; cmp w0,#1; b.ne reject_pid;
         * ldr w8,[sp,#euid]; cbnz w8,reject_uid; cbz echo,normal_reply. */
        unsigned int object=(p[-2]>>16)&31,echo=p[5]&31;
        if((p[-3]&0xffc003ffU)!=0x910003e1U || (p[-2]&0xffe0ffffU)!=0xaa0003e0U ||
           object<19 || object>28 || (p[-1]&0xfc000000U)!=0x94000000U ||
           (p[0]&0xffc003ffU)!=0xb94003e0U || p[1]!=0x7100041fU ||
           (p[2]&0xff00001fU)!=0x54000001U ||
           (p[3]&0xffc003ffU)!=0xb94003e8U || (p[4]&0xff00001fU)!=0x35000008U ||
           (p[5]&0xff000000U)!=0x34000000U || echo<19 || echo>28)continue;
        uint32_t token=(p[-3]>>10)&0xfff;
        if(((p[0]>>10)&0xfff)*4!=token+offsetof(struct darwin_audit_token,pid) ||
           ((p[3]>>10)&0xfff)*4!=token+offsetof(struct darwin_audit_token,euid))continue;
        uint64_t rejected=cache_cb_target(pc+8,p[2]);
        uint64_t rejected_uid=cache_cb_target(pc+16,p[4]);
        uint64_t normal=cache_cb_target(pc+20,p[5]);
        uint32_t reject_code,uid_code,normal_code;
        if(rejected<pc+24 || rejected_uid!=rejected+4 || normal<pc+24 ||
           !cache_policy_instruction(rejected,base,bytes,&reject_code) ||
           !cache_policy_instruction(rejected_uid,base,bytes,&uid_code) ||
           !cache_policy_instruction(normal,base,bytes,&normal_code) ||
           (reject_code&0xfc000000U)!=0x94000000U || (uid_code&0xfc000000U)!=0x94000000U ||
           (normal_code&0xffffffe0U)!=0x52800000U)continue;
        /* The echo arm still extracts the original dictionary fields and calls
         * its real helpers with the same callee-saved object register. */
        if((p[6]&0x9f00001fU)!=0x90000001U || (p[7]&0xffc003ffU)!=0x91000021U ||
           p[8]!=p[-2] || (p[9]&0xfc000000U)!=0x94000000U)continue;
        need(!darwin_native_bootstrap_policy(pc,token,pc+20,rejected),"ambiguous bootstrap policy adapter");
        /* ldr x16,literal; br x16; .quad source-built ABI veneer. Only four
         * instructions are replaced; the original echo branch at p[5] remains. */
        uint32_t jump[]={0x58000050U,0xd61f0200U};
        uint64_t veneer=(uintptr_t)darwin_bootstrap_policy_enter;
        memcpy(p,jump,sizeof(jump));memcpy(p+2,&veneer,sizeof(veneer));
        cache_bootstrap_policy_hooks++;
        fprintf(stderr,"NATIVE_BOOTSTRAP_POLICY address=%#" PRIx64 " resume=%#" PRIx64
            " reject=%#" PRIx64 " token_offset=%u peer=session-provider-pid-uid-incarnation\n",
            pc,pc+20,rejected,token);
    }
}
