/* Linux/AArch64 regression harness for the extracted production jump wrapper.
 * This tests the register/stack handoff, not libpthread or DarlingServer.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

void wqueue_entry_point_asm_jump(void*, int, void*, void*, int, int);
void capture_entry(void*, int, void*, void*, int, int);
void (*wqueue_entry_point)(void*, int, void*, void*, int, int) = capture_entry;

uintptr_t captured[9];
static jmp_buf done;
static void *stack_top, *stack_bottom;
static unsigned iterations;

/* Capture before a C prologue changes SP, FP, or LR. */
__asm__(
    ".text\n"
    ".global capture_entry\n"
    "capture_entry:\n"
    "adrp x9, captured\n"
    "add x9, x9, :lo12:captured\n"
    "stp x0, x1, [x9]\n"
    "stp x2, x3, [x9, #16]\n"
    "stp x4, x5, [x9, #32]\n"
    "mov x10, sp\n"
    "str x10, [x9, #48]\n"
    "stp x29, x30, [x9, #56]\n"
    "b check_entry\n"
);

void check_entry(void)
{
    volatile unsigned char frame[1024];
    frame[0] = 1;
    frame[1023] = 2;
    assert(captured[0] == (uintptr_t)stack_top);
    assert(captured[1] == 123);
    assert(captured[2] == (uintptr_t)stack_bottom);
    assert(captured[3] == 0x1230);
    assert(captured[4] == (iterations ? 1 : 0));
    assert(captured[5] == 7);
    assert(captured[6] == (uintptr_t)stack_top);
    assert(captured[7] == 0 && captured[8] == 0);
    assert(frame[0] + frame[1023] == 3);
    if (++iterations == 10000)
        longjmp(done, 1);
    wqueue_entry_point_asm_jump(stack_top, 123, stack_bottom, (void*)0x1230, 1, 7);
    assert(!"workqueue handoff returned");
}

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    assert(page > 0);
    size_t stack_size = 64 * 1024;
    size_t size = stack_size + 2 * page;
    char *mapping = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    stack_bottom = mapping + page;
    stack_top = mapping + page + stack_size;
    assert(mprotect(stack_bottom, stack_size, PROT_READ | PROT_WRITE) == 0);
    if (!setjmp(done))
        wqueue_entry_point_asm_jump(stack_top, 123, stack_bottom, (void*)0x1230, 0, 7);
    assert(iterations == 10000);
    assert(munmap(mapping, size) == 0);
    puts("PASS: 10000 handoffs preserve arguments, reset SP, and clear FP/LR");
}
