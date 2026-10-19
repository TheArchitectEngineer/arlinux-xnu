/* Entry into the real XNU early initialization path from a Linux process.
 * VM, tasks, interrupts and Mach/BSD dispatch are subsequent bring-up stages. */
#include <arm/cpu_data.h>
#include <arm/cpu_data_internal.h>
#include <arm/cpu_number.h>
#include <kern/thread.h>
#include <kern/assert.h>
#include <kern/startup.h>
#include <kern/ast.h>
#include <mach/machine.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <host/memory.h>
#include <host/time.h>
#include <host/entropy.h>
#include <host/image.h>

int
xnu_host_kernel_early_startup(void)
{
    /* Linux supplies no Apple firmware tree. Original DT queries must report
     * missing properties so original startup code retains its defaults. */
    SecureDTInit(NULL, 0);
    assert(current_thread() == current_thread_volatile());
    assert(current_cpu_datap()->cpu_active_thread == current_thread());
    assert(current_cpu_datap()->cpu_number == 0);
    assert(cpu_number() == 0 && cpu_datap(0) == current_cpu_datap());
    assert(ml_get_cpu_count() == 1 && ml_get_max_cpu_number() == 0);
    assert(ml_get_boot_cpu_number() == cpu_number());
    assert(cpu_type() == CPU_TYPE_ARM64 && slot_type(0) == cpu_type());
    assert(cpu_subtype() == CPU_SUBTYPE_ARM64_ALL && slot_subtype(0) == cpu_subtype());
    assert(cpu_threadtype() == CPU_THREADTYPE_NONE && slot_threadtype(0) == cpu_threadtype());
    /* Exercise the shared AST storage, without pretending to dispatch an AST. */
    ast_t saved_ast = current_cpu_datap()->cpu_pending_ast;
    assert(ast_pending() == &current_cpu_datap()->cpu_pending_ast);
    *ast_pending() = saved_ast | AST_BSD;
    assert(current_cpu_datap()->cpu_pending_ast == (saved_ast | AST_BSD));
    *ast_pending() = saved_ast;
    assert(exception_stack_pointer() != 0);
    xnu_host_image_initialize();
    int error = xnu_host_pmap_bootstrap();
    if (error)
        return error;
    assert(machine_info.max_cpus == ml_get_cpu_count());
    assert(machine_info.physical_cpu == 1 && machine_info.physical_cpu_max == 1);
    assert(machine_info.logical_cpu == 1 && machine_info.logical_cpu_max == 1);
    assert(machine_info.max_mem == ml_get_booter_memory_size());
    assert(machine_info.memory_size == machine_info.max_mem);
    kprintf("XNU_HOST_MACHINE physical=%u logical=%u memory=%llu (Linux backing allocated)\n",
        machine_info.physical_cpu, machine_info.logical_cpu, machine_info.max_mem);
    error = xnu_host_time_bootstrap();
    if (error)
        return error;
    error = xnu_host_entropy_bootstrap();
    if (error)
        return error;
    kernel_startup_bootstrap();
    assert(startup_phase == STARTUP_SUB_LOCKS);
    assert(ml_early_cpu_max_number() == 0);
    kprintf("XNU_HOST_TOPOLOGY cpus=%u boot=%d max=%d type=%#x subtype=%d threadtype=%d AST storage verified (dispatch not connected)\n",
        ml_get_cpu_count(), ml_get_boot_cpu_number(), ml_early_cpu_max_number(),
        cpu_type(), cpu_subtype(), cpu_threadtype());
    kprintf("XNU_EARLY_STARTUP phase=%u debug=%u (VM not initialized)\n",
        startup_phase, startup_debug);
    return 0;
}
