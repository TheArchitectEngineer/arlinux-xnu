/* Replies for the measured native display protocols. Both transports consume
 * this producer; Mach rights and OOL delivery belong to their selected backend. */
#pragma once
/* Measured __BKSDisplayGetMainScreenInfo MIG reply: 60 bytes, not the
 * 68-byte receive allocation (which includes trailer room). The real client
 * copies floats at 36..48 and one byte in each 32-bit slot at 52 and 56.
 * BKSDisplayServicesStart uses either flag to permit empty screen bounds.
 * This backend has a configured nonempty surface and enables neither policy. */
struct darwin_bks_screen_info_reply {
    struct ipc_header header;
    uint32_t ndr[2]; int32_t status;
    uint32_t width,height,scale,rotation;
    uint32_t empty_bounds_policy[2];
};
_Static_assert(sizeof(struct darwin_bks_screen_info_reply)==60,"BKS screen-info wire layout");
_Static_assert(offsetof(struct darwin_bks_screen_info_reply,empty_bounds_policy)==52,"BKS flag offsets");
static unsigned long ipc_display_response(int render,int display,const unsigned char *request,
                                           unsigned long request_size,uint32_t *response,
                                           unsigned long pc,int deallocate)
{
    const struct ipc_header *header=(const void *)request;
    struct ipc_header *out=(void *)response;
    unsigned long response_ool=0;
    if (render && header->id==D_MIG_CA_COPY_DISPLAY_PROPERTIES && request_size==36) {
        uint32_t display_id; __builtin_memcpy(&display_id,request+32,sizeof(display_id));
        response[8]=D_KERN_INVALID_ARGUMENT;
        if (display_id==D_WAYLAND_MAIN_DISPLAY_ID) {
            uint64_t rate=((uint64_t)native_display_refresh*D_CA_MODE_RATE_SCALE+500)/1000;
            uint64_t frame_ns=native_display_refresh ?
                (1000000000000ULL+native_display_refresh-1)/native_display_refresh : 0;
            int live=ipc_display_live();
            response[8]=live ? D_KERN_NOT_SUPPORTED : D_KERN_FAILURE;
            if (live && native_display_refresh && rate<=0xffffff && frame_ns<=UINT32_MAX) {
                uint64_t mode=native_display_width|((uint64_t)native_display_height<<D_CA_MODE_HEIGHT_SHIFT)|(rate<<D_CA_MODE_RATE_SHIFT);
                long address=anonymous_aligned(PAGE16,PROT_READ|PROT_WRITE,0);
                response[8]=D_KERN_RESOURCE_SHORTAGE;
                if ((unsigned long)address<(unsigned long)-4095) {
                    response_ool=address; __builtin_memcpy((void *)address,&mode,sizeof(mode));
                    struct ipc_ool_descriptor descriptor={.address=address,.deallocate=deallocate,
                        .copy=D_MACH_MSG_VIRTUAL_COPY,.type=D_MACH_MSG_OOL_DESCRIPTOR,.size=sizeof(mode)};
                    out->bits|=D_MACH_MSG_COMPLEX;
                    struct darwin_display_properties_prefix *prefix=(void *)response;
                    prefix->descriptor_count=2; prefix->modes=descriptor;
                    /* No hardware shared-memory entry accompanies this
                     * software display. A transferred NULL send right is
                     * the actual absent resource, not a fake device port. */
                    prefix->memory_entry=(struct darwin_display_port){.disposition=D_MACH_TYPE_MOVE_SEND};
                    prefix->ndr[0]=0; prefix->ndr[1]=1;
                    prefix->configuration_seed=1; prefix->connection_seed=1;
                    unsigned char *buffer=(void *)response;
                    unsigned int cursor=sizeof(*prefix);
                    cursor=ipc_display_string(buffer,cursor,"arlinux-wayland-1",sizeof("arlinux-wayland-1"));
                    cursor=ipc_display_string(buffer,cursor,"",sizeof(""));
                    cursor=ipc_display_string(buffer,cursor,"ARLinux Wayland",sizeof("ARLinux Wayland"));
                    struct darwin_display_properties properties={
                        .mode_bytes=sizeof(mode),.current_mode=mode,.preferred_mode=mode,
                        .bounds={0,0,native_display_width,native_display_height},
                        .frame={0,0,native_display_width,native_display_height},
                        .flags=D_CA_DISPLAY_SUPPORTED,.refresh_period_bits=native_display_period_bits,
                        .heartbeat_period_bits=native_display_period_bits,
                        .minimum_frame_duration=frame_ns
                    };
                    __builtin_memcpy(buffer+cursor,&properties,sizeof(properties));
                    out->size=cursor+sizeof(properties);
                    const char label[]="DARWIN_RENDER_PROPERTIES width/height/millihz/mode follows\n";
                    RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
                    trace_call(0,pc,native_display_width); trace_call(1,pc,native_display_height);
                    trace_call(2,pc,native_display_refresh); trace_call(3,pc,mode);
                }
            }
        }
    }
    if (render && header->id==D_MIG_CA_COPY_DISPLAY_LIST &&
        (request_size==sizeof(struct ipc_header) || request_size==36)) {
        /* The measured extended request adds NDR and a zero selector. Serve
         * that default query from our actual registered display. Other
         * selectors are not implemented and must not silently get this list. */
        if (request_size==36) {
            uint32_t selector;__builtin_memcpy(&selector,request+32,sizeof(selector));
            if (selector) { response[8]=D_KERN_INVALID_ARGUMENT;return 0; }
        }
        int live=ipc_display_live();
        response[8]=live ? D_KERN_RESOURCE_SHORTAGE : D_KERN_FAILURE;
        if (live) {
            /* The real CADisplay initializer parses a property-list array
             * and constructs its own remote Display objects. Describe the
             * actual single Wayland resource, not an invented iPhone. */
#define IPC_STRING_INNER(x) #x
#define IPC_STRING(x) IPC_STRING_INNER(x)
            static const char list[]="<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<plist version=\"1.0\"><array><dict>"
                "<key>kCADisplayName</key><string>ARLinux Wayland</string>"
                "<key>kCADisplayDeviceName</key><string>Wayland</string>"
                "<key>kCADisplayId</key><integer>" IPC_STRING(D_WAYLAND_MAIN_DISPLAY_ID) "</integer>"
                "</dict></array></plist>";
#undef IPC_STRING
#undef IPC_STRING_INNER
            long address=anonymous_aligned(PAGE16,PROT_READ|PROT_WRITE,0);
            if ((unsigned long)address<(unsigned long)-4095) {
                response_ool=address;
                for (unsigned int i=0;i<sizeof(list)-1;i++) ((volatile char *)address)[i]=list[i];
                struct ipc_ool_descriptor descriptor={.address=address,.deallocate=deallocate,
                    .copy=D_MACH_MSG_VIRTUAL_COPY,.type=D_MACH_MSG_OOL_DESCRIPTOR,.size=sizeof(list)-1};
                out->bits|=D_MACH_MSG_COMPLEX; out->size=56; response[6]=1;
                __builtin_memcpy(response+7,&descriptor,sizeof(descriptor));
                response[11]=0; response[12]=1; /* NDR */
                response[13]=sizeof(list)-1;
                const char label[]="DARWIN_RENDER_DISPLAY_LIST id/address/bytes follows\n";
                RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
                trace_call(0,pc,D_WAYLAND_MAIN_DISPLAY_ID); trace_call(1,pc,address); trace_call(2,pc,sizeof(list)-1);
            }
        }
    }
    if (display && (header->id==D_MIG_BKS_GET_MAIN_SCREEN_INFO ||
                    header->id==D_MIG_BKS_GET_MAIN_SCREEN_INFO_EXTENDED) && request_size==sizeof(struct ipc_header)) {
        response[8]=ipc_display_live() ? D_KERN_SUCCESS : D_KERN_FAILURE;
        if (!response[8]) {
            /* Actual iOS 12 client: 24-byte request, 52-byte reply; four
             * binary32 values. GSSetMainScreenInfo converts width/height,
             * uses scale for pixel-to-point conversion and the last float
             * as a rotation angle. Our wl_surface uses default scale 1
             * and normal transform; dimensions came from xdg configure. */
            out->size=52;
            response[9]=ipc_display_float(native_display_width);
            response[10]=ipc_display_float(native_display_height);
            response[11]=0x3f800000U; /* 1.0: unscaled buffer pixels */
            response[12]=0; /* 0.0: normal, unrotated surface */
            if (header->id==D_MIG_BKS_GET_MAIN_SCREEN_INFO_EXTENDED) {
                struct darwin_bks_screen_info_reply *screen=(void *)response;
                out->size=sizeof(*screen);
                screen->empty_bounds_policy[0]=0;
                screen->empty_bounds_policy[1]=0;
            }
            const char label[]="DARWIN_DISPLAY_QUERY width/height follows\n";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            trace_call(0,pc,native_display_width); trace_call(1,pc,native_display_height);
        }
    }
    return response_ool;
}
