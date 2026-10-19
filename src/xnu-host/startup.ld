/* XNU's original startup code sorts this table in place. The ELF port
 * declares its entries writable. VM bring-up retains all original phases
 * through ZALLOC; full linkage retains every phase from the same objects. */
SECTIONS
{
  PROVIDE (__xnu_text_start = ADDR(.text));
  PROVIDE (__xnu_text_end = ADDR(.text) + SIZEOF(.text));
  .xnu_startup_entries : ALIGN(8)
  {
    PROVIDE ("section$start$__BOOTDATA$__init_entry_set" = .);
#if XNU_HOST_VM_STAGE
    KEEP(*(.xnu_startup.TUNABLES))
    KEEP(*(.xnu_startup.TIMEOUTS))
    KEEP(*(.xnu_startup.LOCKS))
    KEEP(*(.xnu_startup.KPRINTF))
    KEEP(*(.xnu_startup.PMAP_STEAL))
    KEEP(*(.xnu_startup.KMEM))
    KEEP(*(.xnu_startup.ZALLOC))
#else
    KEEP(*(.xnu_startup.*))
#endif
    PROVIDE ("section$end$__BOOTDATA$__init_entry_set" = .);
  }
  .xnu_percpu : ALIGN(16384)
  {
    PROVIDE ("section$start$__DATA$__percpu" = .);
    KEEP(*("__DATA, __percpu"))
    . = ALIGN(16384);
    PROVIDE ("section$end$__DATA$__percpu" = .);
  }
  /* Preserve compiler-generated allocation views, including callsites removed
   * by function GC. XNU's real allocator will assign their zones during boot. */
  .xnu_kalloc_type : ALIGN(8)
  {
    PROVIDE ("section$start$__DATA_CONST$__kalloc_type" = .);
    KEEP(*("__DATA_CONST, __kalloc_type, regular, live_support"))
    PROVIDE ("section$end$__DATA_CONST$__kalloc_type" = .);
  }
  .xnu_kalloc_var : ALIGN(8)
  {
    PROVIDE ("section$start$__DATA_CONST$__kalloc_var" = .);
    KEEP(*("__DATA_CONST, __kalloc_var, regular, live_support"))
    PROVIDE ("section$end$__DATA_CONST$__kalloc_var" = .);
  }
#if XNU_HOST_VM_STAGE
  /DISCARD/ : { *(.xnu_startup.*) }
#endif
}
INSERT AFTER .data;
