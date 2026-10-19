/* Freestanding interactive Darwin console program. The native compatibility
 * journey exercises actual VM/COW/wait, TLS and normal exit before dyld exists.
 * Uses Apple's raw BSD/platform calling conventions, no Linux SVC calls. */
.text
.p2align 2
.globl _start
_start:
    sub sp,sp,#32
    mov x0,#0
    mov x1,#32768
    mov x2,#3
    mov x3,#0x1002
    mov x4,#-1
    mov x5,#0
    mov x16,#197
    svc #0x80
    b.cs failure
    tst x0,#16383
    b.ne failure
    mov x20,x0
    mov x9,#0x123
    str x9,[x20]
    mov x9,#0x456
    str x9,[x20,#16384]
    bl set_tsd
    bl check_tsd
    mrs x9,TPIDR_EL0           // Darwin CPU metadata, CPU index must be zero
    cbnz x9,failure
    mov x16,#2
    svc #0x80
    b.cs failure
    cbnz x1,child
    mov x21,x0
    mov x1,sp
    mov x2,#0
    mov x3,#0
    mov x16,#7
    svc #0x80
    b.cs failure
    cmp x0,x21
    b.ne failure
    ldr w9,[sp]
    mov w10,#(37<<8)
    cmp w9,w10
    b.ne failure
    bl check_tsd
    ldr x9,[x20]
    cmp x9,#0x123
    b.ne failure
    ldr x9,[x20,#16384]
    cmp x9,#0x456
    b.ne failure
    add x0,x20,#16384
    mov x1,#16384
    mov x2,#1
    mov x16,#74
    svc #0x80
    b.cs failure
    adrp x1,welcome@PAGE
    add x1,x1,welcome@PAGEOFF
    adrp x2,welcome_end@PAGE
    add x2,x2,welcome_end@PAGEOFF
    bl print
    mov x23,#0
input:
    mov x0,#0
    add x1,x20,#32
    mov x2,#1
    mov x16,#3
    svc #0x80
    b.cs failure
    cmp x0,#1
    b.ne failure
    bl check_tsd
    ldrb w9,[x20,#32]
    cmp w9,#'q'
    b.eq finished
    cmp w9,#'+'
    b.ne input
    add x23,x23,#1
    cmp x23,#10
    b.ne digit
    mov x23,#0
digit:
    adrp x1,counter@PAGE
    add x1,x1,counter@PAGEOFF
    adrp x2,counter_end@PAGE
    add x2,x2,counter_end@PAGEOFF
    bl print
    add w9,w23,#'0'
    strb w9,[x20,#33]
    mov w9,#10
    strb w9,[x20,#34]
    add x1,x20,#33
    add x2,x1,#2
    bl print
    b input
finished:
    bl release
    adrp x1,goodbye@PAGE
    add x1,x1,goodbye@PAGEOFF
    adrp x2,goodbye_end@PAGE
    add x2,x2,goodbye_end@PAGEOFF
    bl print
    mov x0,#0
    b exit
child:
    add x20,x20,#16384
    mov x0,x20
    bl set_tsd
    bl check_tsd
    mov x9,#0x789
    str x9,[x20]
    sub x20,x20,#16384
    bl release                // child TLS now points into unmapped memory
    mov x0,#37
    b exit
set_tsd:
    mov x3,#2
    mov x16,#0x80000000
    svc #0x80
    b.cs failure
    ret
check_tsd:
    mrs x9,TPIDRRO_EL0
    cmp x9,x20
    b.ne failure
    ret
release:
    mov x0,x20
    mov x1,#32768
    mov x16,#73
    svc #0x80
    b.cs failure
    ret
print:
    sub x2,x2,x1
    mov x22,x2
    mov x0,#1
    mov x16,#4
    svc #0x80
    b.cs failure
    cmp x0,x22
    b.ne failure
    ret
failure:
    mov x0,#99
exit:
    mov x16,#1
    svc #0x80
    brk #0
.section __TEXT,__const
welcome:
    .ascii "Darwin console: memory/COW/TLS ready; + increments, q exits\n"
welcome_end:
counter:
    .ascii "counter="
counter_end:
goodbye:
    .ascii "Darwin console: normal exit\n"
goodbye_end:
