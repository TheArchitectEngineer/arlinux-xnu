/* Single virtual CPU context. Kernel code sees real XNU thread/cpu records;
 * the Linux host owns the execution stack and the signal alternative stack. */
#include <arm/cpu_data.h>
#include <arm/cpu_data_internal.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <host/context.h>

static struct thread boot_thread;
static cpu_data_t boot_cpu;
static __thread thread_t host_thread;
static __thread boolean_t host_interrupts_enabled;

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
    boot_cpu.cpu_number = 0;
    boot_cpu.cpu_active_thread = &boot_thread;
    boot_cpu.cpu_active_stack = (vm_offset_t)stack_base;
    boot_cpu.intstack_top = (vm_offset_t)exception_base + exception_size;
    boot_cpu.excepstack_top = boot_cpu.intstack_top;
    boot_thread.machine.CpuDatap = &boot_cpu;
    boot_thread.machine.kstackptr = (vm_offset_t)stack_base + stack_size;
    boot_thread.machine.preemption_count = 0; /* ARM early boot permits mutex locking */
    boot_thread.kernel_stack = (vm_offset_t)stack_base;
    boot_thread.state = TH_RUN;
    machine_set_current_thread(&boot_thread);
    host_interrupts_enabled = FALSE;
    kprintf("XNU_HOST_CONTEXT cpu=%d thread=%p preemption=%u stack=%lu exception=%lu\n",
        boot_cpu.cpu_number, host_thread, boot_thread.machine.preemption_count,
        stack_size, exception_size);
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

int
get_preemption_level(void)
{
    assert(host_thread != THREAD_NULL);
    return (int)host_thread->machine.preemption_count;
}

unsigned int
get_preemption_level_for_thread(thread_t thread)
{
    return thread->machine.preemption_count;
}

void
_disable_preemption(void)
{
    assert(host_thread && host_thread->machine.preemption_count != ~0u);
    host_thread->machine.preemption_count++;
}

void
_enable_preemption(void)
{
    assert(host_thread && host_thread->machine.preemption_count);
    host_thread->machine.preemption_count--;
    /* Pending AST dispatch will be attached to the host scheduler event loop. */
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
