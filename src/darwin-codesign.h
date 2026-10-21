#pragma once
#include <stdint.h>

/* Original input metadata, retained in the loader's read-only mapping for the
 * process lifetime. Presence is not signature authentication or Apple trust. */
struct darwin_code_metadata {
    uint32_t signature_bytes;
    const unsigned char *xml,*der;
    uint32_t xml_bytes,der_bytes;
};
