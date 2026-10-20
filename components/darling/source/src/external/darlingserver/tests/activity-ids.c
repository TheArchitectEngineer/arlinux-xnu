/* Link against production traps.c, ipc_voucher.c and OSAtomicOperations.c
 * objects with -Wl,--gc-sections -pthread. copyout/panic are mocked: this is
 * allocator/trap coverage, not a cross-process RPC integration test.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void mach_init_activity_id(void);
extern int dtape_mach_generate_activity_id(uint32_t, int32_t, uint64_t);

__attribute__((noreturn)) void panic(const char *format, ...)
{
    (void)format;
    abort();
}

int copyout(const void *source, uint64_t destination, size_t size)
{
    if (destination <= 1)
        return EFAULT;
    memcpy((void *)(uintptr_t)destination, source, size);
    return 0;
}

enum { THREADS = 8, REQUESTS = 1000 };
struct range { uint64_t first; unsigned count; };
static struct range ranges[THREADS][REQUESTS];

static void *allocate(void *argument)
{
    unsigned worker = (unsigned)(uintptr_t)argument;
    for (unsigned i = 0; i < REQUESTS; ++i) {
        struct range *r = &ranges[worker][i];
        r->count = (i + worker) % 16 + 1;
        assert(dtape_mach_generate_activity_id(0, r->count,
            (uintptr_t)&r->first) == 0);
    }
    return NULL;
}

static int compare(const void *a, const void *b)
{
    uint64_t x = ((const struct range *)a)->first;
    uint64_t y = ((const struct range *)b)->first;
    return (x > y) - (x < y);
}

int main(void)
{
    uint64_t id = UINT64_MAX;
    mach_init_activity_id();
    assert(dtape_mach_generate_activity_id(0, 0, (uintptr_t)&id) == 4);
    assert(dtape_mach_generate_activity_id(0, -1, (uintptr_t)&id) == 4);
    assert(dtape_mach_generate_activity_id(0, 17, (uintptr_t)&id) == 4);
    assert(id == UINT64_MAX);
    assert(dtape_mach_generate_activity_id(0, 0, 0) == 4);
    assert(dtape_mach_generate_activity_id(0, 1, (uintptr_t)&id) == 0 && id == 1);
    assert(dtape_mach_generate_activity_id(0, 16, (uintptr_t)&id) == 0 && id == 2);
    assert(dtape_mach_generate_activity_id(0, 2, (uintptr_t)&id) == 0 && id == 18);
    assert(dtape_mach_generate_activity_id(0, 1, 0) == EFAULT);
    assert(dtape_mach_generate_activity_id(0, 1, 1) == EFAULT);
    assert(dtape_mach_generate_activity_id(0, 1, (uintptr_t)&id) == 0);
    uint64_t next = id + 1;

    pthread_t threads[THREADS];
    for (unsigned i = 0; i < THREADS; ++i)
        assert(pthread_create(&threads[i], NULL, allocate, (void *)(uintptr_t)i) == 0);
    for (unsigned i = 0; i < THREADS; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    struct range sorted[THREADS * REQUESTS];
    memcpy(sorted, ranges, sizeof sorted);
    qsort(sorted, THREADS * REQUESTS, sizeof sorted[0], compare);
    for (unsigned i = 0; i < THREADS * REQUESTS; ++i) {
        assert(sorted[i].first == next);
        next += sorted[i].count;
    }
    puts("PASS: count validation, range bases, copyout errors, 8000 concurrent reservations");
}
