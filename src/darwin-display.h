/* Measured CADisplay property wire layout. Variable string fields precede
 * this tail; every member is pack(4), including unaligned 64-bit fields.
 * Source: genuine client recorded in run 39354313051, not class ivar offsets. */
#pragma once
enum {
    D_CA_MODE_HEIGHT_SHIFT=16,D_CA_MODE_RATE_SHIFT=32,D_CA_MODE_RATE_SCALE=65536,
    D_CA_DISPLAY_SUPPORTED=1
};
struct darwin_display_port {
    uint32_t name,padding; uint16_t reserved; uint8_t disposition,type;
} __attribute__((packed,aligned(4)));
struct darwin_display_properties_prefix {
    struct ipc_header header; uint32_t descriptor_count;
    struct ipc_ool_descriptor modes;
    struct darwin_display_port memory_entry;
    uint32_t ndr[2],configuration_seed,connection_seed;
} __attribute__((packed,aligned(4)));
_Static_assert(sizeof(struct darwin_display_properties_prefix)==72,"CADisplay property prefix ABI");
struct darwin_display_rect { int32_t x,y,width,height; };
struct darwin_display_properties {
    uint32_t mode_bytes;
    uint64_t current_mode,preferred_mode;
    struct darwin_display_rect bounds,frame;
    uint32_t flags,unimplemented_0[2],overscan_bits[2],unimplemented_1;
    uint64_t refresh_period_bits,heartbeat_period_bits;
    uint32_t minimum_frame_duration,unimplemented_2[12];
} __attribute__((packed,aligned(4)));
_Static_assert(sizeof(struct darwin_display_properties)==144,"CADisplay property tail ABI");
_Static_assert(offsetof(struct darwin_display_properties,bounds)==20,"CADisplay bounds ABI");
_Static_assert(offsetof(struct darwin_display_properties,refresh_period_bits)==76,"CADisplay period ABI");

/* Current display-info MIG reply: three OOL descriptors and one memory-entry
 * port. The inline strings are packed (64/64/256 maxima), followed by this
 * 308-byte tail. Source: original client decoder/consumer in run 39481604703.
 * Unimplemented fields stay absent/zero; their capabilities are not advertised. */
struct darwin_display_properties_extended_prefix {
    struct ipc_header header; uint32_t descriptor_count;
    struct ipc_ool_descriptor modes,supported_hdr,preferred_hdr;
    struct darwin_display_port memory_entry;
    uint32_t ndr[2],configuration_seed,connection_seed;
} __attribute__((packed,aligned(4)));
struct darwin_display_properties_extended {
    uint32_t mode_bytes,supported_hdr_bytes,preferred_hdr_bytes,mode_count,unimplemented_0;
    uint64_t current_mode,preferred_mode;
    uint32_t unimplemented_1; uint64_t latency_bits;
    struct darwin_display_rect bounds,frame;
    uint32_t flags,unimplemented_2[2],overscan_bits[2],overscan_adjustment;
    uint32_t logical_scale_bits[2],unimplemented_3[2],point_scale_bits,physical_size_bits[2],unimplemented_4[2];
    uint64_t refresh_period_bits,heartbeat_period_bits,unimplemented_period_bits;
    uint32_t minimum_frame_duration,unimplemented_tail[35];
} __attribute__((packed,aligned(4)));
_Static_assert(sizeof(struct darwin_display_properties_extended_prefix)==104,"Extended CADisplay prefix ABI");
_Static_assert(sizeof(struct darwin_display_properties_extended)==308,"Extended CADisplay tail ABI");
_Static_assert(offsetof(struct darwin_display_properties_extended,bounds)==48,"Extended CADisplay bounds ABI");
_Static_assert(offsetof(struct darwin_display_properties_extended,flags)==80,"Extended CADisplay flags ABI");
_Static_assert(offsetof(struct darwin_display_properties_extended,point_scale_bits)==120,"Extended CADisplay scale ABI");
_Static_assert(offsetof(struct darwin_display_properties_extended,refresh_period_bits)==140,"Extended CADisplay period ABI");

/* A MIG string is offset/count followed by count bytes padded to four bytes.
 * All callers supply local NUL-terminated strings and the fixed IPC buffer. */
static unsigned int ipc_display_string(unsigned char *buffer,unsigned int cursor,
                                        const char *text,unsigned int bytes)
{
    uint32_t header[2]={0,bytes};
    __builtin_memcpy(buffer+cursor,header,sizeof(header)); cursor+=sizeof(header);
    for (unsigned int i=0;i<bytes;i++) buffer[cursor+i]=text[i];
    return cursor+((bytes+3)&~3U);
}
