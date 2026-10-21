/* Opt-in bounded observations of actual header-only Mach sends and pure
 * receives. No payload, guest state, queue ownership or outcome is changed. */
#pragma once
struct native_mach_wakeup_observation {
    uint32_t port,sends_started,sends_completed,send_errors;
    uint32_t waits_started,waits_completed,received,received_on;
    uint32_t message_id,send_tid,receive_tid,last_error;
    uint64_t send_result,wait_result;
};
static struct native_mach_wakeup_observation native_mach_wakeups[64];
static unsigned int native_mach_wakeups_omitted;
static struct native_mach_wakeup_observation *native_mach_wakeup(uint32_t port)
{
    if(!native_diagnose_syscalls || !port)return NULL;
    for(unsigned int i=0;i<sizeof(native_mach_wakeups)/sizeof(*native_mach_wakeups);i++) {
        uint32_t value=__atomic_load_n(&native_mach_wakeups[i].port,__ATOMIC_ACQUIRE);
        if(!value && __atomic_compare_exchange_n(&native_mach_wakeups[i].port,&value,port,0,
                                                __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))value=port;
        if(value==port)return native_mach_wakeups+i;
    }
    if(!__atomic_fetch_add(&native_mach_wakeups_omitted,1,__ATOMIC_RELAXED))
        trace_record("DARLING_WAKEUP_OBSERVATIONS_TRUNCATED limit=0x",64,0,0);
    return NULL;
}
