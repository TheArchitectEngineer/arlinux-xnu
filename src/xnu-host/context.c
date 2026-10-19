/* Single virtual CPU context. Kernel code sees real XNU thread/cpu records;
 * the Linux host owns the execution stack and the signal alternative stack. */
#include <arm/cpu_data.h>
#include <arm/cpu_data_internal.h>
#include <arm/cpu_number.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <kern/processor.h>
#include <kern/ast.h>
#include <kern/startup.h>
#include <mach/machine.h>
#include <machine/machine_routines.h>
#include <host/context.h>
#include <arm64/usermode_context.h>
#include <pexpert/pexpert.h>

static struct thread boot_thread;
cpu_data_t PERCPU_DATA(cpu_data);
struct percpu_base percpu_base;
#define boot_cpu (*PERCPU_GET_MASTER(cpu_data))
static __thread thread_t host_thread;
static __thread boolean_t host_interrupts_enabled;

unsigned long
xnu_host_kernel_stack_size(void)
{
    /* The native stack must exist before original TUNABLES/stack_init runs.
     * Use its same configured page count; stack_init will establish caches
     * and recompute these values once the startup dispatcher reaches it. */
    uint32_t pages = KERNEL_STACK_SIZE / PAGE_SIZE;
    PE_parse_boot_argn("kernel_stack_pages", &pages, sizeof(pages));
    kernel_stack_size = (vm_offset_t)pages * PAGE_SIZE;
    assert(kernel_stack_size >= PAGE_SIZE &&
        (kernel_stack_size & (kernel_stack_size - 1)) == 0);
    kernel_stack_mask = ~(kernel_stack_size - 1);
    return kernel_stack_size;
}

unsigned long
xnu_host_thread_stack_top(thread_t thread)
{
    assert(thread != THREAD_NULL && thread->kernel_stack != 0);
    assert(thread->machine.kstackptr == (vm_offset_t)thread_get_kernel_state(thread));
    return thread->machine.kstackptr;
}

void *
xnu_host_thread_kernel_context(thread_t thread)
{
    return &((thread_kernel_state_t)xnu_host_thread_stack_top(thread))->machine;
}

unsigned long
xnu_host_kernel_stack_top(void)
{
    unsigned long top = xnu_host_thread_stack_top(host_thread);
    thread_initialize_kernel_state(host_thread);
    return top;
}

void
xnu_host_continuation_returned(void)
{
    panic("kernel continuation returned after thread termination");
}

vm_offset_t
current_percpu_base(void)
{
    assert(host_thread != THREAD_NULL);
    return host_thread->machine.pcpu_data_base;
}

vm_offset_t
other_percpu_base(int cpu)
{
    assert(cpu == 0);
    return (vm_offset_t)cpu_datap(cpu) - __PERCPU_ADDR(cpu_data);
}

processor_t
current_processor(void)
{
    return PERCPU_GET(processor);
}

/* Same generic ARM policy: the XNU scheduler's selected processor is retained.
 * No host CPU identifier or Linux thread substitutes for the virtual processor. */
processor_t
machine_choose_processor(processor_set_t pset, processor_t processor)
{
    (void)pset;
    return processor;
}

uint64_t
ml_get_backtrace_pc(struct arm_saved_state *state)
{
    assert(state != NULL && is_saved_state64(state));
    return get_saved_state_pc(state);
}


cpu_data_t *
cpu_datap(int cpu)
{
    assert(cpu == 0); /* This host has exactly one virtual CPU. */
    return &boot_cpu;
}

int
cpu_number(void)
{
    assert(host_thread != THREAD_NULL);
    return current_cpu_datap()->cpu_number;
}

/* Describe the guest's one virtual CPU, not Linux's host topology. */
unsigned int
ml_get_cpu_count(void)
{
    return MAX_CPUS;
}

int
ml_get_max_cpu_number(void)
{
    return MAX_CPUS - 1;
}

int
ml_get_boot_cpu_number(void)
{
    return boot_cpu.cpu_number;
}

int
ml_early_cpu_max_number(void)
{
    assert(startup_phase >= STARTUP_SUB_TUNABLES);
    return ml_get_max_cpu_number();
}

cpu_type_t slot_type(int slot) { return cpu_datap(slot)->cpu_type; }
cpu_subtype_t slot_subtype(int slot) { return cpu_datap(slot)->cpu_subtype; }
cpu_threadtype_t slot_threadtype(int slot) { return cpu_datap(slot)->cpu_threadtype; }
cpu_type_t cpu_type(void) { return current_cpu_datap()->cpu_type; }
cpu_subtype_t cpu_subtype(void) { return current_cpu_datap()->cpu_subtype; }
cpu_threadtype_t cpu_threadtype(void) { return current_cpu_datap()->cpu_threadtype; }

ast_t *
ast_pending(void)
{
    return &current_cpu_datap()->cpu_pending_ast;
}

thread_t
xnu_host_current_thread(void)
{
    return host_thread;
}

/* Some XNU callers use the exported accessor rather than the locks.h macro. */
thread_t
(current_thread)(void)
{
    return host_thread;
}

void
machine_set_current_thread(thread_t thread)
{
    assert(thread != THREAD_NULL);
    thread->machine.CpuDatap = &boot_cpu;
    thread->machine.pcpu_data_base = 0; /* Master CPU uses the static ELF section. */
    boot_cpu.cpu_active_thread = thread;
    boot_cpu.cpu_active_stack = thread->kernel_stack;
    host_thread = thread;
}

vm_offset_t
xnu_host_exception_stack_pointer(void)
{
    return boot_cpu.excepstack_top;
}

int
xnu_host_context_initialize(void *stack_base, unsigned long stack_size,
    void *exception_base, unsigned long exception_size)
{
    assert(MAX_CPUS == 1);
    assert(host_thread == THREAD_NULL);
    assert(stack_base && stack_size && exception_base && exception_size);
    assert((vm_offset_t)&boot_cpu >= percpu_section_start() &&
        (vm_offset_t)(&boot_cpu + 1) <= percpu_section_end());
    percpu_base.size = percpu_section_size();
    /* Original single-CPU layout: iterate the master slot once, no replicas. */
    percpu_base.start = VM_MAX_KERNEL_ADDRESS;
    percpu_base.end = 0;
    /* Initialize the original kern/machine.c singleton from the one CPU
     * Linux actually hosts. Physical memory is recorded after allocation. */
    machine_info.max_cpus = MAX_CPUS;
    machine_info.physical_cpu = machine_info.physical_cpu_max = 1;
    machine_info.logical_cpu = machine_info.logical_cpu_max = 1;
    boot_cpu.cpu_number = 0;
    boot_cpu.cpu_type = CPU_TYPE_ARM64;
    boot_cpu.cpu_subtype = CPU_SUBTYPE_ARM64_ALL;
    boot_cpu.cpu_threadtype = CPU_THREADTYPE_NONE;
    boot_cpu.cpu_active_thread = &boot_thread;
    boot_cpu.cpu_active_stack = (vm_offset_t)stack_base;
    boot_cpu.intstack_top = (vm_offset_t)exception_base + exception_size;
    boot_cpu.excepstack_top = boot_cpu.intstack_top;
    boot_thread.machine.CpuDatap = &boot_cpu;
    boot_thread.machine.kstackptr = (vm_offset_t)stack_base + stack_size -
        sizeof(struct thread_kernel_state);
    boot_thread.machine.preemption_count = 0; /* ARM early boot permits mutex locking */
    boot_thread.kernel_stack = (vm_offset_t)stack_base;
    boot_thread.state = TH_RUN;
    machine_set_current_thread(&boot_thread);
    assert(PERCPU_GET(cpu_data) == &boot_cpu && other_percpu_base(0) == 0);
    unsigned int cpu_records = 0;
    percpu_foreach(cpu, cpu_data) {
        assert(cpu == &boot_cpu);
        cpu_records++;
    }
    assert(cpu_records == 1);
    host_interrupts_enabled = FALSE;
    kprintf("XNU_HOST_CONTEXT cpu=%d thread=%p preemption=%u stack=%lu exception=%lu\n",
        boot_cpu.cpu_number, host_thread, boot_thread.machine.preemption_count,
        stack_size, exception_size);
    kprintf("XNU_HOST_PERCPU section=[%#lx,%#lx) size=%lu records=%u (master CPU)\n",
        percpu_section_start(), percpu_section_end(), percpu_base.size, cpu_records);
    return 0;
}

void
xnu_host_context_adopt_boot_thread(void)
{
    assert(host_thread != THREAD_NULL);
    thread_t thread = thread_bootstrap();
    thread->kernel_stack = host_thread->kernel_stack;
    thread->machine.kstackptr = host_thread->machine.kstackptr;
    thread->machine.preemption_count = 0;
    machine_set_current_thread(thread);
    kprintf("XNU_HOST_CONTEXT adopted XNU thread_bootstrap thread=%p\n", thread);
}

boolean_t
ml_get_interrupts_enabled(void)
{
    return host_interrupts_enabled;
}

boolean_t
ml_set_interrupts_enabled(boolean_t enable)
{
    boolean_t old = host_interrupts_enabled;
    if (enable && !old) {
        assert(host_thread != THREAD_NULL && boot_cpu.cpu_int_state == NULL);
        /* Keep ARM's real preemption semantics before unmasking the virtual
         * interrupt state. DAIF belongs to Linux; pending XNU ASTs do not. */
        if (get_preemption_level() == 0) {
            while (host_thread->machine.CpuDatap->cpu_pending_ast & AST_URGENT)
                ast_taken_kernel();
        }
    }
    host_interrupts_enabled = !!enable;
    return old;
}

boolean_t
ml_set_interrupts_enabled_with_debug(boolean_t enable, boolean_t debug)
{
    (void)debug; /* hardware timing instrumentation is unused by this host */
    return ml_set_interrupts_enabled(enable);
}

boolean_t
ml_early_set_interrupts_enabled(boolean_t enable)
{
    return ml_set_interrupts_enabled(enable);
}

boolean_t
ml_at_interrupt_context(void)
{
    return boot_cpu.cpu_int_state != NULL;
}

/* The original ARM path sends an IPI only when targeting another processor.
 * There is no other processor in this port. Pending local ASTs are consumed
 * at enable-preemption/unmask and user-return boundaries. */
void
cause_ast_check(processor_t processor)
{
    assert(processor != PROCESSOR_NULL && processor == current_processor());
}

void
ml_get_power_state(boolean_t *in_interrupt, boolean_t *idle)
{
    processor_t processor = current_processor();
    *in_interrupt = ml_at_interrupt_context();
    *idle = processor != PROCESSOR_NULL && processor->state == PROCESSOR_IDLE;
}
