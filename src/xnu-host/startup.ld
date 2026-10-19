/* XNU's original startup code sorts this table in place. The ELF port
 * declares its entries writable and keeps all linked initialization records. */
SECTIONS
{
  .xnu_startup_entries : ALIGN(8)
  {
    PROVIDE ("section$start$__BOOTDATA$__init_entry_set" = .);
    KEEP(*("__BOOTDATA,__init_entry_set"))
    PROVIDE ("section$end$__BOOTDATA$__init_entry_set" = .);
  }
}
INSERT AFTER .data;
