/* Real XNU PID 1 journey, shared by the reference and future usermode boot.
 * No libc, loader shim or host syscall replacement. Upstream ios-sweet's
 * mkinit.py wraps this relocation-free section into a signed static Mach-O.
 */
.text
.p2align 2
.globl _start
_start:
    mov x9, sp
    and x9, x9, #~15
    mov sp, x9
    sub sp, sp, #16
    adr x0, console
    mov x1, #2
    mov x2, #0
    mov x16, #5                 // open /dev/console O_RDWR
    svc #0x80
    b.cs silent_failure
    mov x19, x0
    mov x16, #20                // getpid
    svc #0x80
    b.cs failure
    cmp x0, #1
    b.ne failure
    adr x1, started
    adr x2, started_end
    bl print

    mov x0, #0
    mov x1, #32768              // two 16 KiB pages
    mov x2, #3                  // PROT_READ | PROT_WRITE
    mov x3, #0x1002             // MAP_ANON | MAP_PRIVATE
    mov x4, #-1
    mov x5, #0
    mov x16, #197               // mmap
    svc #0x80
    b.cs failure
    mov x20, x0
    mov x9, #0x123
    str x9, [x20]
    mov x9, #0x456
    str x9, [x20, #16384]
    mov x16, #2                 // fork: child x0=0, parent x0=child pid
    svc #0x80
    b.cs failure
    cbz x0, child
    mov x21, x0
    str wzr, [sp]
    mov x1, sp
    mov x2, #0
    mov x3, #0
    mov x16, #7                 // wait4(child, &status, 0, NULL)
    svc #0x80
    b.cs failure
    cmp x0, x21
    b.ne failure
    ldr w9, [sp]
    cmp w9, #(37 << 8)          // normal exit, not a fatal signal
    b.ne failure
    ldr x9, [x20]
    cmp x9, #0x123              // child writes must have COW isolation
    b.ne failure
    ldr x9, [x20, #16384]
    cmp x9, #0x456
    b.ne failure
    bl release
passed:
    adr x1, success
    adr x2, success_end
    bl print
    bl sleep
    b passed                   // keep PID 1 and visible success alive

child:
    mov x9, #0x789
    str x9, [x20]
    str x9, [x20, #16384]
    ldr x10, [x20]
    cmp x10, x9
    b.ne child_failure
    ldr x10, [x20, #16384]
    cmp x10, x9
    b.ne child_failure
    mov x0, x20
    mov x1, #32768
    mov x16, #73                // munmap in the child
    svc #0x80
    b.cs child_failure
    adr x1, child_done
    adr x2, child_done_end
    bl print
    mov x0, #37
    b exit_child
child_failure:
    mov x0, #99
exit_child:
    mov x16, #1
    svc #0x80
    brk #0

release:
    mov x0, x20
    mov x1, #32768
    mov x16, #73                // munmap in the parent
    svc #0x80
    b.cs failure
    ret
print:
    sub x2, x2, x1
    mov x22, x2
    mov x0, x19
    mov x16, #4
    svc #0x80
    b.cs failure
    cmp x0, x22
    b.ne failure
    ret
sleep:
    mov x0, #0
    mov x1, #0
    mov x2, #0
    mov x3, #0
    adr x4, delay
    mov x16, #93                // select timeout; real scheduler idle/wake
    svc #0x80
    b.cs failure
    ret
failure:
    mov x0, x19
    adr x1, failed
    adr x2, failed_end
    sub x2, x2, x1
    mov x16, #4
    svc #0x80
silent_failure:
    b silent_failure           // no false normal exit of PID 1

.p2align 3
delay:
    .quad 5, 0
console:
    .asciz "/dev/console"
started:
    .ascii "XNU journey: PID 1 started\n"
started_end:
child_done:
    .ascii "XNU journey: child freed memory, exit 37\n"
child_done_end:
success:
    .ascii "XNU JOURNEY PASS: memory COW exit wait\n"
success_end:
failed:
    .ascii "XNU JOURNEY FAIL\n"
failed_end:
