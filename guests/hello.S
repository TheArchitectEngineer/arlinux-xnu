// Freestanding ARM64 Darwin executable. No Apple SDK or runtime libraries.
.text
.p2align 2
.globl _start
_start:
    // Darwin write(1, message, length); x8 deliberately names Linux close.
    mov x0, #1
    adr x1, message
    adr x3, message_end
    sub x2, x3, x1
    mov x8, #57
    mov x16, #4
    cmp xzr, xzr
    svc #0x80
    b.cs failure
    adr x3, message
    adr x4, message_end
    sub x4, x4, x3
    cmp x0, x4
    b.ne failure

    // Verify Darwin error ABI: positive EBADF=9, carry set.
    mov x0, #99
    adr x1, message
    mov x2, #1
    mov x16, #4
    svc #0x80
    b.cc failure
    cmp x0, #9
    b.ne failure

    // getpid returns a real per-process identity, not a fixture constant.
    mov x16, #20
    svc #0x80
    b.cs failure
    cmp x0, #0
    b.le failure
    mov x0, #0
    b exit_guest
failure:
    mov x0, #42
exit_guest:
    mov x16, #1
    svc #0x80
    brk #0
message:
    .ascii "Hello from ARM64 Mach-O on Linux (Darwin ABI)\n"
message_end:
