/* Instruction transport only. Every stop returns to the real kernel caller;
 * callbacks never dispatch Darwin calls or reenter/schedule XNU from TCG. */
#include <host/guest.h>
#include <usermode_vm.h>
#include <unicorn/unicorn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct xnu_host_guest {
    uc_engine *engine;
    struct xnu_host_guest_event event;
    bool running;
    uc_err stop_error;
};

static void
exception_stop(uc_engine *engine, uint32_t exception, void *opaque)
{
    struct xnu_host_guest *guest = opaque;
    guest->event.reason = XNU_HOST_GUEST_EXCEPTION;
    guest->event.exception = exception;
    guest->stop_error = uc_emu_stop(engine);
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
    guest->stop_error = uc_emu_stop(engine);
    /* Do not claim to resolve a fault. The XNU caller must resolve it and
     * explicitly update this pmap before resuming the faulting instruction. */
    return false;
}

int
xnu_host_guest_create(struct xnu_host_guest **result)
{
    if (!result)
        return UC_ERR_ARG;
    *result = NULL;
    struct xnu_host_guest *guest = calloc(1, sizeof(*guest));
    if (!guest)
        return UC_ERR_NOMEM;
    uc_err error = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &guest->engine);
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
    *result = guest;
    return 0;
}

int
xnu_host_guest_destroy(struct xnu_host_guest *guest)
{
    if (!guest || guest->running)
        return UC_ERR_ARG;
    uc_err error = uc_close(guest->engine);
    free(guest);
    return error;
}

static bool
valid_page(struct xnu_host_guest *guest, uint64_t address)
{
    /* Low guest VA is isolated in the engine, never MAP_FIXED over host code.
     * Exact task address bounds are enforced by XNU's user map, not invented here. */
    return guest && !guest->running && !(address & (XNU_HOST_PAGE_SIZE - 1)) &&
        address <= UINT64_MAX - XNU_HOST_PAGE_SIZE;
}

int
xnu_host_guest_map(struct xnu_host_guest *guest, unsigned long long address,
    void *frame, unsigned int protection)
{
    if (!valid_page(guest, address) || !frame || protection > UC_PROT_ALL ||
        ((uintptr_t)frame & (XNU_HOST_PAGE_SIZE - 1)))
        return UC_ERR_ARG;
    return uc_mem_map_ptr(guest->engine, address, XNU_HOST_PAGE_SIZE,
        protection, frame);
}

int
xnu_host_guest_unmap(struct xnu_host_guest *guest, unsigned long long address)
{
    if (!valid_page(guest, address))
        return UC_ERR_ARG;
    return uc_mem_unmap(guest->engine, address, XNU_HOST_PAGE_SIZE);
}

int
xnu_host_guest_protect(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int protection)
{
    if (!valid_page(guest, address) || protection > UC_PROT_ALL)
        return UC_ERR_ARG;
    return uc_mem_protect(guest->engine, address, XNU_HOST_PAGE_SIZE, protection);
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

int
xnu_host_guest_run(struct xnu_host_guest *guest,
    struct xnu_host_guest_state *state, unsigned long instructions,
    struct xnu_host_guest_event *event)
{
    if (!guest || guest->running || !state || !event || !instructions ||
        (state->pc & 3) || (state->pstate & 0x1f)) /* AArch64 EL0t only. */
        return UC_ERR_ARG;
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
    } else if (error == UC_ERR_INSN_INVALID) {
        guest->event.reason = XNU_HOST_GUEST_UNDEFINED;
    } else if (error) {
        return error;
    }
    error = transfer_state(guest->engine, state, false);
    if (error)
        return error;
    *event = guest->event;
    return 0;
}
