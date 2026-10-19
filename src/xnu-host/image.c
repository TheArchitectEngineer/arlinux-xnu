/* The host kernel is a fixed-address ELF image, not an Apple kernel collection. */
#include <mach/vm_param.h>
#include <arm/usermode_vm.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <host/image.h>

extern char __executable_start[], _end[], __xnu_text_start[], __xnu_text_end[];
vm_offset_t vm_kernel_base, vm_kernel_top, vm_kernel_stext, vm_kernel_etext;
vm_offset_t vm_kernel_slide, vm_kernel_slid_base, vm_kernel_slid_top;
vm_offset_t vm_kernel_builtinkmod_text, vm_kernel_builtinkmod_text_end;

vm_offset_t
ml_static_unslide(vm_offset_t address)
{
    return VM_KERNEL_IS_SLID(address) ? address - vm_kernel_slide : 0;
}

vm_offset_t
ml_static_slide(vm_offset_t address)
{
    vm_offset_t slid = address + vm_kernel_slide;
    return VM_KERNEL_IS_SLID(slid) ? slid : 0;
}

void
xnu_host_image_initialize(void)
{
    assert(vm_kernel_base == 0);
    vm_kernel_base = vm_kernel_slid_base = (vm_offset_t)__executable_start;
    vm_kernel_top = vm_kernel_slid_top = (vm_offset_t)_end;
    vm_kernel_stext = (vm_offset_t)__xnu_text_start;
    vm_kernel_etext = (vm_offset_t)__xnu_text_end;
    vm_kernel_slide = 0; /* The runner links this kernel with -no-pie. */
    /* No built-in kext image is present: represent an empty interval. */
    vm_kernel_builtinkmod_text = vm_kernel_builtinkmod_text_end = vm_kernel_top;
    assert(vm_kernel_base == XNU_HOST_KERNEL_BASE);
    assert(vm_kernel_base <= vm_kernel_stext && vm_kernel_stext < vm_kernel_etext &&
        vm_kernel_etext <= vm_kernel_top && vm_kernel_top < XNU_HOST_PHYSICAL_VIEW);
    vm_offset_t entry = (vm_offset_t)xnu_host_image_initialize;
    assert(entry >= vm_kernel_stext && entry < vm_kernel_etext);
    assert(ml_static_slide(ml_static_unslide(entry)) == entry);
    assert(ml_static_unslide(XNU_HOST_HEAP_BASE) == 0);
    kprintf("XNU_HOST_IMAGE ELF=[%#lx,%#lx) text=[%#lx,%#lx) slide=%lu (no kexts)\n",
        vm_kernel_base, vm_kernel_top, vm_kernel_stext, vm_kernel_etext, vm_kernel_slide);
}
