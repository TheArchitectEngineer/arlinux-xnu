/* XNU sorts this writable table in place. Real bootstrap must retain every
 * compiled registration: dropping post-ZALLOC entries would falsely advance
 * startup phases without running their initializers. */
SECTIONS
{
  PROVIDE (__xnu_text_start = ADDR(.text));
  PROVIDE (__xnu_text_end = ADDR(.text) + SIZEOF(.text));
  .xnu_startup_entries : ALIGN(8)
  {
    PROVIDE ("section$start$__BOOTDATA$__init_entry_set" = .);
    KEEP(*(.xnu_startup.*))
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
