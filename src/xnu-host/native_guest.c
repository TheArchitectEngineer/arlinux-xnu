/* Instruction transport only. Every stop returns to the real kernel caller;
 * callbacks never dispatch Darwin calls or reenter/schedule XNU from TCG. */
#define _GNU_SOURCE
#include <host/guest.h>
#include <host/context.h>
#include <host/memory.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <usermode_vm.h>
#include <unicorn/unicorn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct guest_mapping {
    unsigned long long address;
    unsigned int page, protection;
    struct guest_mapping *next;
};

struct xnu_host_guest {
    uc_engine *engine;
    struct xnu_host_guest_event event;
    bool running, poisoned;
    struct guest_mapping *mappings, *spare;
    struct xnu_host_guest *next;
    uc_err stop_error;
};

static struct xnu_host_guest *guests;

/* Indices are from pinned qemu/target/arm/cpu.h. The fork exposes the
 * pending syndrome/address before cpu-exec clears exception_index. Never
 * infer ESR from intno or read stale architectural ESR_EL1/FAR_EL1 banks. */
static void
exception_stop(uc_engine *engine, uint32_t exception, void *opaque)
{
    struct xnu_host_guest *guest = opaque;
    guest->event.reason = XNU_HOST_GUEST_EXCEPTION;
    guest->event.exception = exception;
    uc_err error = UC_ERR_OK;
    if (exception == 1 || exception == 2 || exception == 3 ||
        exception == 4 || exception == 7) {
        uint32_t syndrome;
        uint64_t address = 0;
        error = uc_reg_read(engine, UC_ARM64_REG_EXCEPTION_SYNDROME, &syndrome);
        if (!error) {
            unsigned int ec = syndrome >> 26;
            if (ec == 0x20 || ec == 0x21 || ec == 0x22 ||
                ec == 0x24 || ec == 0x25 || ec == 0x26 || ec == 0x34 || ec == 0x35)
                error = uc_reg_read(engine, UC_ARM64_REG_EXCEPTION_VADDRESS, &address);
        }
        if (!error) {
            guest->event.esr = syndrome;
            guest->event.address = address;
            guest->event.syndrome_valid = 1;
        }
    }
    /* Internal/yield/semihost events are not ARM synchronous exceptions. */
    uc_err stop = uc_emu_stop(engine);
    guest->stop_error = error ? error : stop;
}

static bool
memory_stop(uc_engine *engine, uc_mem_type type, uint64_t address,
    int size, int64_t value, void *opaque)
{
    (void)value;
    struct xnu_host_guest *guest = opaque;
    guest->event.reason = XNU_HOST_GUEST_MEMORY;
    guest->event.address = address;
    guest->event.size = size;
    guest->event.access = type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT ? 2 :
        type == UC_MEM_FETCH_UNMAPPED || type == UC_MEM_FETCH_PROT ? 4 : 1;
    guest->event.protection_fault = type == UC_MEM_WRITE_PROT ||
        type == UC_MEM_READ_PROT || type == UC_MEM_FETCH_PROT;
    /* Flat engine translations model a 16 KiB leaf. ISV stays clear; access
     * type and translation/permission failure are the information we own. */
    uint32_t ec = guest->event.access == 4 ? 0x20u : 0x24u;
    guest->event.esr = (ec << 26) | (1u << 25) |
        (guest->event.protection_fault ? 0xfu : 0x7u) |
        (guest->event.access == 2 ? (1u << 6) : 0);
    guest->event.syndrome_valid = 1;
    guest->stop_error = uc_emu_stop(engine);
    /* Do not claim to resolve a fault. The XNU caller must resolve it and
     * explicitly update this pmap before resuming the faulting instruction. */
    return false;
}

static int
guest_create(struct xnu_host_guest **result)
{
    if (!result)
        return UC_ERR_ARG;
    *result = NULL;
    struct xnu_host_guest *guest = calloc(1, sizeof(*guest));
    if (!guest)
        return UC_ERR_NOMEM;
    uc_err error = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &guest->engine);
    uint32_t cpacr = 3u << 20; /* FP/SIMD enabled for the restored 128-bit state. */
    if (!error)
        error = uc_reg_write(guest->engine, UC_ARM64_REG_CPACR_EL1, &cpacr);
    uc_hook hook;
    if (!error)
        error = uc_hook_add(guest->engine, &hook, UC_HOOK_INTR,
            exception_stop, guest, 1, 0);
    if (!error)
        error = uc_hook_add(guest->engine, &hook, UC_HOOK_MEM_INVALID,
            memory_stop, guest, 1, 0);
    if (error) {
        if (guest->engine)
            uc_close(guest->engine);
        free(guest);
        return error;
    }
    guest->next = guests;
    guests = guest;
    *result = guest;
    return 0;
}

static int
guest_destroy(struct xnu_host_guest *guest)
{
    if (!guest || guest->running || guest->poisoned || guest->mappings)
        return UC_ERR_ARG; /* XNU must remove mappings before final release. */
    struct xnu_host_guest **link = &guests;
    while (*link && *link != guest)
        link = &(*link)->next;
    if (!*link)
        return UC_ERR_ARG;
    uc_err error = uc_close(guest->engine);
    if (error) {
        guest->poisoned = true;
        return error;
    }
    *link = guest->next;
    free(guest->spare);
    free(guest);
    return 0;
}

static bool
valid_page(struct xnu_host_guest *guest, uint64_t address)
{
    return guest && !guest->running && !guest->poisoned &&
        !(address & (XNU_HOST_PAGE_SIZE - 1)) &&
        address <= UINT64_MAX - XNU_HOST_PAGE_SIZE;
}

static struct guest_mapping **
find_mapping(struct xnu_host_guest *guest, uint64_t address)
{
    struct guest_mapping **link = &guest->mappings;
    while (*link && (*link)->address != address)
        link = &(*link)->next;
    return link;
}

static int
guest_map(struct xnu_host_guest *guest, unsigned long long address,
    void *frame, unsigned int protection)
{
    if (!valid_page(guest, address) || protection > UC_PROT_ALL)
        return UC_ERR_ARG;
    unsigned int page = xnu_host_memory_frame_number(frame);
    if (!page)
        return UC_ERR_ARG; /* Only real managed physical backing is accepted. */
    struct guest_mapping **link = find_mapping(guest, address);
    if (*link)
        return UC_ERR_MAP; /* Replacement needs an explicit XNU removal. */
    struct guest_mapping *mapping = guest->spare;
    guest->spare = NULL;
    if (!mapping)
        mapping = calloc(1, sizeof(*mapping));
    if (!mapping)
        return UC_ERR_NOMEM; /* No engine mapping or page reference changed. */
    if (xnu_host_memory_guest_reference(page, 1)) {
        free(mapping);
        return UC_ERR_RESOURCE;
    }
    *mapping = (struct guest_mapping){ .address = address, .page = page,
        .protection = protection };
    *link = mapping;
    uc_err error = uc_mem_map_ptr(guest->engine, address, XNU_HOST_PAGE_SIZE,
        protection, frame);
    if (error)
        /* Keep the reference on uncertain native mutation. The caller must
         * stop, never recycle this frame after a failed insertion. */
        guest->poisoned = true;
    return error;
}

static int
guest_unmap(struct xnu_host_guest *guest, unsigned long long address)
{
    if (!valid_page(guest, address))
        return UC_ERR_ARG;
    struct guest_mapping **link = find_mapping(guest, address);
    if (!*link)
        return UC_ERR_MAP;
    uc_err error = uc_mem_unmap(guest->engine, address, XNU_HOST_PAGE_SIZE);
    if (error) {
        guest->poisoned = true;
        return error;
    }
    struct guest_mapping *mapping = *link;
    if (xnu_host_memory_guest_reference(mapping->page, -1)) {
        guest->poisoned = true;
        return UC_ERR_RESOURCE;
    }
    *link = mapping->next;
    free(mapping);
    return 0;
}

static int
guest_protect(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int protection)
{
    if (!valid_page(guest, address) || protection > UC_PROT_ALL)
        return UC_ERR_ARG;
    struct guest_mapping *mapping = *find_mapping(guest, address);
    if (!mapping)
        return UC_ERR_MAP;
    uc_err error = uc_mem_protect(guest->engine, address, XNU_HOST_PAGE_SIZE, protection);
    if (error)
        guest->poisoned = true;
    else
        mapping->protection = protection;
    return error;
}

static int
guest_page_protect(unsigned int page, unsigned int protection)
{
    for (struct xnu_host_guest *guest = guests; guest; guest = guest->next) {
        struct guest_mapping *mapping = guest->mappings;
        while (mapping) {
            struct guest_mapping *next = mapping->next;
            if (mapping->page == page) {
                int error = protection ? guest_protect(guest, mapping->address,
                    mapping->protection & protection) :
                    guest_unmap(guest, mapping->address);
                if (error)
                    return error;
            }
            mapping = next;
        }
    }
    return 0;
}

static int
guest_query(struct xnu_host_guest *guest, unsigned long long start,
    unsigned long long end, unsigned long long *resident)
{
    if (!guest || guest->running || guest->poisoned || !resident || start > end ||
        ((start | end) & (XNU_HOST_PAGE_SIZE - 1)))
        return UC_ERR_ARG;
    unsigned long long bytes = 0;
    for (struct guest_mapping *mapping = guest->mappings; mapping; mapping = mapping->next)
        if (mapping->address >= start && mapping->address < end)
            bytes += XNU_HOST_PAGE_SIZE;
    *resident = bytes;
    return 0;
}

static int
guest_lookup(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int *page)
{
    if (!guest || guest->running || guest->poisoned || !page)
        return UC_ERR_ARG;
    address &= ~(XNU_HOST_PAGE_SIZE - 1ULL);
    struct guest_mapping *mapping = *find_mapping(guest, address);
    *page = mapping ? mapping->page : 0;
    return 0;
}

static int
guest_reserve(struct xnu_host_guest *guest)
{
    if (!guest || guest->running || guest->poisoned)
        return UC_ERR_ARG;
    if (!guest->spare)
        guest->spare = calloc(1, sizeof(*guest->spare));
    return guest->spare ? 0 : UC_ERR_NOMEM;
}

/* Pure status inspection, no TCG/libc call or stack switch. An insertion OOM
 * is retryable only before any possibly effective native mapping mutation. */
int
xnu_host_guest_error_retryable(struct xnu_host_guest *guest, int error)
{
    return guest && !guest->running && !guest->poisoned && error == UC_ERR_NOMEM;
}

static uc_err
transfer_state(uc_engine *engine, struct xnu_host_guest_state *state, bool write)
{
    int regs[72];
    void *values[72];
    unsigned int n = 0;
#define REG(id, field) do { regs[n] = (id); values[n++] = &(field); } while (0)
    for (unsigned int i = 0; i < 29; i++)
        REG(UC_ARM64_REG_X0 + i, state->x[i]);
    REG(UC_ARM64_REG_X29, state->x[29]);
    REG(UC_ARM64_REG_X30, state->x[30]);
    REG(UC_ARM64_REG_SP, state->sp);
    REG(UC_ARM64_REG_PC, state->pc);
    REG(UC_ARM64_REG_PSTATE, state->pstate);
    for (unsigned int i = 0; i < 32; i++)
        REG(UC_ARM64_REG_Q0 + i, state->q[i]);
    REG(UC_ARM64_REG_TPIDR_EL0, state->tpidr);
    REG(UC_ARM64_REG_TPIDRRO_EL0, state->tpidrro);
    REG(UC_ARM64_REG_FPCR, state->fpcr);
    REG(UC_ARM64_REG_FPSR, state->fpsr);
#undef REG
    return write ? uc_reg_write_batch(engine, regs, values, n) :
        uc_reg_read_batch(engine, regs, values, n);
}

static int
guest_run(struct xnu_host_guest *guest,
    struct xnu_host_guest_state *state, unsigned long instructions,
    struct xnu_host_guest_event *event)
{
    if (!guest || guest->running || guest->poisoned || !state || !event || !instructions ||
        (state->pstate & 0x1f)) /* AArch64 EL0t only. */
        return UC_ERR_ARG;
    if (state->pc & 3) {
        *event = (struct xnu_host_guest_event){ .reason = XNU_HOST_GUEST_EXCEPTION,
            .esr = (0x22u << 26) | (1u << 25), .syndrome_valid = 1, .address = state->pc };
        return 0;
    }
    uc_err error = transfer_state(guest->engine, state, true);
    if (error)
        return error;
    /* XNU can modify physical backing through a different VA. Flush translated
     * code before every resume, including aliases and later executable mappings. */
    error = uc_ctl_flush_tb(guest->engine);
    if (error)
        return error;
    memset(&guest->event, 0, sizeof(guest->event));
    guest->stop_error = UC_ERR_OK;
    guest->running = true;
    error = uc_emu_start(guest->engine, state->pc, UINT64_MAX, 0, instructions);
    guest->running = false;
    if (guest->stop_error)
        return guest->stop_error;
    if (guest->event.reason == XNU_HOST_GUEST_MEMORY) {
        /* These are the engine's unresolved memory exceptions, not transport
         * failures. Preserve their faulting PC for the real kernel dispatcher. */
        if (error != UC_ERR_OK && error != UC_ERR_READ_UNMAPPED &&
            error != UC_ERR_WRITE_UNMAPPED && error != UC_ERR_FETCH_UNMAPPED &&
            error != UC_ERR_READ_PROT && error != UC_ERR_WRITE_PROT &&
            error != UC_ERR_FETCH_PROT)
            return error;
    } else if (guest->event.reason == XNU_HOST_GUEST_EXCEPTION) {
        /* Keep the CPU's captured syndrome, including sysreg/FP/alignment
         * traps. A generic emulator exception result must not overwrite it. */
        if (error != UC_ERR_OK && error != UC_ERR_EXCEPTION &&
            error != UC_ERR_INSN_INVALID && error != UC_ERR_READ_UNALIGNED &&
            error != UC_ERR_WRITE_UNALIGNED && error != UC_ERR_FETCH_UNALIGNED)
            return error;
    } else if (error == UC_ERR_INSN_INVALID) {
        guest->event.reason = XNU_HOST_GUEST_UNDEFINED;
        guest->event.esr = 1u << 25;
        guest->event.syndrome_valid = 1;
    } else if (error) {
        return error;
    }
    error = transfer_state(guest->engine, state, false);
    if (error)
        return error;
    *event = guest->event;
    return 0;
}

/* TCG and libc must not run on XNU's small, reference-tracked kernel stacks.
 * One native CPU stack services all engines. It is allocated by host main,
 * before XNU bootstrap, and guarded at both ends. No callbacks reenter XNU. */
static unsigned long native_stack_top;
static _Thread_local bool in_native;
static _Thread_local struct {
    enum { CREATE, DESTROY, MAP, UNMAP, PROTECT, RUN, PAGE_PROTECT, QUERY, LOOKUP, RESERVE } operation;
    struct xnu_host_guest *guest, **result;
    unsigned long long address, end, *resident;
    unsigned int *found;
    void *frame;
    unsigned int protection, page;
    struct xnu_host_guest_state *state;
    unsigned long instructions;
    struct xnu_host_guest_event *event;
} request;

int
xnu_host_guest_initialize(void)
{
    if (native_stack_top)
        return EALREADY;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return EINVAL;
    const unsigned long bytes = 1024 * 1024;
    if (bytes % (unsigned long)page)
        return EINVAL;
    void *mapping = mmap(NULL, bytes + 2 * page, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (mapping == MAP_FAILED)
        return errno;
    if (mprotect((char *)mapping + page, bytes, PROT_READ | PROT_WRITE)) {
        int error = errno;
        munmap(mapping, bytes + 2 * page);
        return error;
    }
    native_stack_top = (unsigned long)mapping + page + bytes;
    return 0;
}

static int
perform(void)
{
    switch (request.operation) {
    case CREATE: return guest_create(request.result);
    case DESTROY: return guest_destroy(request.guest);
    case MAP: return guest_map(request.guest, request.address, request.frame, request.protection);
    case UNMAP: return guest_unmap(request.guest, request.address);
    case PROTECT: return guest_protect(request.guest, request.address, request.protection);
    case RUN: return guest_run(request.guest, request.state, request.instructions, request.event);
    case PAGE_PROTECT: return guest_page_protect(request.page, request.protection);
    case QUERY: return guest_query(request.guest, request.address, request.end, request.resident);
    case LOOKUP: return guest_lookup(request.guest, request.address, request.found);
    case RESERVE: return guest_reserve(request.guest);
    }
    return UC_ERR_ARG;
}

static int
dispatch(void)
{
    if (!native_stack_top)
        return UC_ERR_RESOURCE;
    in_native = true;
    int error = xnu_host_run_on_stack(perform, native_stack_top);
    in_native = false;
    return error;
}

int
xnu_host_guest_create(struct xnu_host_guest **result)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = CREATE; request.result = result;
    return dispatch();
}

int
xnu_host_guest_destroy(struct xnu_host_guest *guest)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = DESTROY; request.guest = guest;
    return dispatch();
}

int
xnu_host_guest_map(struct xnu_host_guest *guest, unsigned long long address,
    void *frame, unsigned int protection)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = MAP; request.guest = guest; request.address = address;
    request.frame = frame; request.protection = protection;
    return dispatch();
}

int
xnu_host_guest_unmap(struct xnu_host_guest *guest, unsigned long long address)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = UNMAP; request.guest = guest; request.address = address;
    return dispatch();
}

int
xnu_host_guest_protect(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int protection)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = PROTECT; request.guest = guest; request.address = address;
    request.protection = protection;
    return dispatch();
}

int
xnu_host_guest_run(struct xnu_host_guest *guest,
    struct xnu_host_guest_state *state, unsigned long instructions,
    struct xnu_host_guest_event *event)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = RUN; request.guest = guest; request.state = state;
    request.instructions = instructions; request.event = event;
    return dispatch();
}

int
xnu_host_guest_page_protect(unsigned int page, unsigned int protection)
{
    if (in_native || !page || protection > UC_PROT_ALL)
        return UC_ERR_ARG;
    request.operation = PAGE_PROTECT; request.page = page; request.protection = protection;
    return dispatch();
}

int
xnu_host_guest_query(struct xnu_host_guest *guest, unsigned long long start,
    unsigned long long end, unsigned long long *resident)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = QUERY; request.guest = guest;
    request.address = start; request.end = end; request.resident = resident;
    return dispatch();
}

int
xnu_host_guest_lookup(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int *page)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = LOOKUP; request.guest = guest;
    request.address = address; request.found = page;
    return dispatch();
}

int
xnu_host_guest_reserve(struct xnu_host_guest *guest)
{
    if (in_native) return UC_ERR_ARG;
    request.operation = RESERVE; request.guest = guest;
    return dispatch();
}
