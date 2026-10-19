/* XNU's original startup code sorts this table in place. The ELF port
 * declares its entries writable and keeps all linked initialization records. */
SECTIONS
{
  PROVIDE (__xnu_text_start = ADDR(.text));
  PROVIDE (__xnu_text_end = ADDR(.text) + SIZEOF(.text));
  .xnu_startup_entries : ALIGN(8)
  {
    PROVIDE ("section$start$__BOOTDATA$__init_entry_set" = .);
    KEEP(*("__BOOTDATA,__init_entry_set"))
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
}
INSERT AFTER .data;
