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
// Entered by a tail branch immediately after libxpc's real audit getter.
// Its LR is the exact mapped predicate address. Preserve the original frame,
// callee-saved echo flag/object registers and actual PID/euid scratch values.
.global darwin_bootstrap_policy_enter
.hidden darwin_bootstrap_policy_enter
.type darwin_bootstrap_policy_enter,%function
darwin_bootstrap_policy_enter:
    stp x29,x30,[sp,#-16]!
    mov x29,sp
    add x0,sp,#16
    mov x1,x30
    bl darwin_native_bootstrap_route
    mov x16,x0
    mov w0,w1
    lsr x8,x1,#32
    ldp x29,x30,[sp],#16
    br x16
.size darwin_bootstrap_policy_enter,.-darwin_bootstrap_policy_enter
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
// CGRect is an ARM64 homogeneous floating-point aggregate (d0-d3), even
// though the syscall runtime itself is compiled with general registers only.
// Save its exact bits without using host TLS or converting its values.
.global darwin_objc_rect
.hidden darwin_objc_rect
.type darwin_objc_rect,%function
darwin_objc_rect:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    str x19,[sp,#16]
    mov x19,x3
    blr x2
    stp d0,d1,[x19]
    stp d2,d3,[x19,#16]
    ldr x19,[sp,#16]
    ldp x29,x30,[sp],#32
    ret
.size darwin_objc_rect,.-darwin_objc_rect
.global darwin_ui_context_flip
.hidden darwin_ui_context_flip
.type darwin_ui_context_flip,%function
darwin_ui_context_flip:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    stp x19,x20,[sp,#16]
    mov x19,x0
    mov x20,x2
    fmov d0,xzr
    ucvtf d1,w3
    blr x1
    mov x0,x19
    fmov d0,#1.0
    fmov d1,#-1.0
    blr x20
    ldp x19,x20,[sp,#16]
    ldp x29,x30,[sp],#32
    ret
.size darwin_ui_context_flip,.-darwin_ui_context_flip
.section .note.GNU-stack,"",%progbits
