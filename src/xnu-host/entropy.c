/* Linux provides the entropy/PRNG state. These routines take no XNU locks;
 * host syscalls may block without depending on any guest lock. No floating
 * point or deterministic fallback generator is used. */
#include <mach/mach_types.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/misc_protos.h>
#include <libkern/crypto/rand.h>
#include <sys/random.h>
#include <host/entropy.h>

static void
host_random(void *buffer, size_t length)
{
    int error = xnu_host_entropy_fill(buffer, length);
    if (error)
        panic("Linux entropy provider failed error=%d", error);
}

void
read_random(void *buffer, u_int length)
{
    host_random(buffer, length);
}

void
read_frandom(void *buffer, u_int length)
{
    host_random(buffer, length);
}

uint64_t
early_random(void)
{
    uint64_t value;
    host_random(&value, sizeof(value));
    return value;
}

int
cc_rand_generate(void *buffer, size_t length)
{
    return xnu_host_entropy_fill(buffer, length);
}

int
random_buf(void *buffer, size_t length)
{
    return cc_rand_generate(buffer, length);
}

size_t
crypto_random_kmem_ctx_size(void)
{
    /* A provider identifier, not a seed. Linux owns the generator state. */
    return sizeof(uintptr_t);
}

void
crypto_random_kmem_init(crypto_random_ctx_t context)
{
    assert(context != NULL);
    unsigned char readiness;
    host_random(&readiness, sizeof(readiness));
    *(uintptr_t *)context = 1;
}

void
crypto_random_generate(crypto_random_ctx_t context, void *buffer, size_t length)
{
    assert(context != NULL && *(uintptr_t *)context == 1);
    host_random(buffer, length);
}

void
crypto_random_uniform(crypto_random_ctx_t context, uint64_t bound, uint64_t *result)
{
    assert(bound != 0 && result != NULL);
    uint64_t value;
    uint64_t threshold = -bound % bound;
    do {
        crypto_random_generate(context, &value, sizeof(value));
    } while (value < threshold);
    *result = value % bound;
}

int
xnu_host_entropy_bootstrap(void)
{
    unsigned char buffer[32];
    read_random(buffer, sizeof(buffer));
    read_frandom(buffer, sizeof(buffer));
    (void)early_random();
    assert(cc_rand_generate(buffer, sizeof(buffer)) == 0);
    assert(random_buf(buffer, sizeof(buffer)) == 0);
    uintptr_t context;
    assert(crypto_random_kmem_ctx_size() <= sizeof(context));
    assert(crypto_random_kmem_ctx_size() <= CRYPTO_RANDOM_MAX_CTX_SIZE);
    crypto_random_kmem_init(&context);
    const uint64_t bounds[] = { 1, 3, 10, 0x8000000000000001ULL, UINT64_MAX };
    for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); i++) {
        uint64_t value;
        crypto_random_uniform(&context, bounds[i], &value);
        assert(value < bounds[i]);
    }
    kprintf("XNU_HOST_ENTROPY Linux getrandom read/uniform passed bytes=%llu (original VM not initialized)\n",
        xnu_host_entropy_bytes());
    return 0;
}
