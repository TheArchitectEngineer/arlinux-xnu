#pragma once
#include <stdint.h>

/* Original input metadata, retained in read-only storage for process lifetime.
 * DER may be a serialization of the embedded XML when no DER slot exists.
 * Presence is not signature authentication or Apple trust. */
struct darwin_code_metadata {
    uint32_t signature_bytes;
    const unsigned char *xml,*der;
    uint32_t xml_bytes,der_bytes;
};
