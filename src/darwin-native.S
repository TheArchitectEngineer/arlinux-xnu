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
// Linux clone with a separate runtime stack and clear-child-tid lifecycle.
.global darwin_clone_thread
.hidden darwin_clone_thread
.type darwin_clone_thread,%function
darwin_clone_thread:
    mov x9,x0
    ldr x1,[x9]
    sub x1,x1,#16
    str x9,[x1]
    add x2,x9,#56
    mov x4,x2
    mov x3,xzr
    movz x0,#0x0f00
    movk x0,#0x35,lsl #16
    mov x8,#220
    svc #0
.global darwin_clone_return
.hidden darwin_clone_return
darwin_clone_return:
    cbz x0,1f
    ret
1:  ldr x0,[sp]
    bl darwin_thread_boot
    brk #1
.size darwin_clone_thread,.-darwin_clone_thread

.global darwin_thread_enter
.hidden darwin_thread_enter
.type darwin_thread_enter,%function
darwin_thread_enter:
    mov x9,x1
    mov sp,x0
    mov x0,x2
    mov x1,x3
    mov x2,x4
    mov x3,x5
    mov x4,x6
    mov x5,x7
    mov x29,xzr
    mov x30,xzr
    br x9
.size darwin_thread_enter,.-darwin_thread_enter
.section .note.GNU-stack,"",%progbits
