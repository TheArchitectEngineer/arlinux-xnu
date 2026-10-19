/* Linux platform properties use XNU's original Device Tree query API.
 * No Apple firmware tree is supplied at boot; absence is reported explicitly
 * and the caller keeps its existing default or command-line setting. */
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <string.h>

int
PE_current_console(PE_Video *info)
{
    /* Linux terminal backend: no boot framebuffer or pixel dimensions. The
     * original km TTY retains its headless character-grid fallback. */
    memset(info, 0, sizeof(*info));
    return 0;
}

boolean_t
PE_get_default(const char *name, void *destination, unsigned int capacity)
{
    DTEntry defaults;
    const void *value;
    unsigned int size;
    if (SecureDTLookupEntry(NULL, "/defaults", &defaults) != kSuccess ||
        SecureDTGetProperty(defaults, name, &value, &size) != kSuccess ||
        size > capacity)
        return FALSE;
    memcpy(destination, value, size);
    return TRUE;
}
