/* Shared native ARM64 instruction adaptation; no libc or host TLS use. */
#pragma once
static size_t rewrite_instruction_words(uint32_t *instructions,uint64_t count)
{
    size_t rewritten=0;
    for (uint64_t k=0;k<count;k++) {
        uint32_t word=instructions[k];
        if ((word&~31U)==0xd53bd060U) {
            instructions[k]=0xd53bd040U|(word&31); /* Darwin TSD -> Linux TLS */
            rewritten++;
        } else if ((word&~31U)==0xd53bd040U) {
            instructions[k]=0xd2800000U|(word&31); /* single CPU metadata */
            rewritten++;
        }
    }
    return rewritten;
}
