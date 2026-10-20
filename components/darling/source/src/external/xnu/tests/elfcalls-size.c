#include "../darling/src/libsystem_kernel/emulation/include/linux_premigration/elfcalls_size.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(elfcalls_parse_size(NULL)==0);
    const char *invalid[]={"", "0x10", "-1", "+1", " 10", "10 ", "1g", "ffffffffffffffffffffffffffffffff"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i)
        assert(elfcalls_parse_size(invalid[i])==0);
    assert(elfcalls_parse_size("1aF")==431);
    char maximum[2*sizeof(size_t)+1];
    snprintf(maximum,sizeof(maximum),"%zx",SIZE_MAX);
    assert(elfcalls_parse_size(maximum)==SIZE_MAX);
    assert(elfcalls_size_from_apple(NULL)==0);
    const char *none[]={"elf_calls=1234",NULL};
    const char *valid[]={"elf_calls_size=120","elf_calls=1234",NULL};
    const char *duplicate[]={"elf_calls_size=120","elf_calls_size=120",NULL};
    const char *bad[]={"elf_calls_size=invalid",NULL};
    assert(elfcalls_size_from_apple(none)==0);
    assert(elfcalls_size_from_apple(valid)==0x120);
    assert(elfcalls_size_from_apple(duplicate)==0);
    assert(elfcalls_size_from_apple(bad)==0);
    puts("PASS: strict hexadecimal sizes, overflow, absent and duplicate metadata");
}
