.text
.global darwin_raw
.hidden darwin_raw
.type darwin_raw,%function
darwin_raw:
    mov x8,x0
    mov x0,x1
    mov x1,x2
    mov x2,x3
    mov x3,x4
    mov x4,x5
    mov x5,x6
    svc #0
.global darwin_raw_return
.hidden darwin_raw_return
darwin_raw_return:
    ret
.size darwin_raw,.-darwin_raw

.global darwin_native_enter
.type darwin_native_enter,%function
darwin_native_enter:
    // exec starts with no Darwin TSD until userland establishes it. Do not
    // expose glibc's TCB to rewritten Darwin TSD reads. No libc calls follow.
    msr tpidr_el0,xzr
    mov sp,x0
    mov x29,xzr
    mov x30,xzr
    mov x0,xzr
    br x1
.size darwin_native_enter,.-darwin_native_enter
.section .note.GNU-stack,"",%progbits
