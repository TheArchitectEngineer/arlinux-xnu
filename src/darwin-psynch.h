/* The observed libpthread first-fit mutex slow path. Userland owns its E-bit,
 * generation counts and owner CAS. Each unlock-drop posts exactly one wake
 * credit; a late waiter consumes the prepost instead of losing the wake.
 * Linux futex waits park actual threads. Fair-share/shared/condition/RW-lock
 * operations are not silently mapped to this different first-fit protocol. */
#pragma once
struct native_mutex_wait { uint64_t address; int credits; };
static struct native_mutex_wait native_mutex_waits[D_PSYNCH_WAIT_OBJECTS];
static long native_psynch_mutex(const unsigned long long *r)
{
    if (((r[4]>>D_MUTEX_POLICY_SHIFT)&D_MUTEX_POLICY_MASK)!=D_MUTEX_FIRSTFIT ||
        (r[4]&D_MUTEX_SHARED)) return -ENOTSUP;
    unsigned char byte;
    if (!r[0] || guest_copy(&byte,r[0],1,0)) return -EFAULT;
    struct native_mutex_wait *wait=NULL;
    for (unsigned int i=0;i<D_PSYNCH_WAIT_OBJECTS;i++) {
        uint64_t address=__atomic_load_n(&native_mutex_waits[i].address,__ATOMIC_ACQUIRE);
        if (address==r[0]) { wait=native_mutex_waits+i; break; }
    }
    if (!wait) for (unsigned int i=0;i<D_PSYNCH_WAIT_OBJECTS;i++) {
        uint64_t expected=0;
        if (__atomic_compare_exchange_n(&native_mutex_waits[i].address,&expected,r[0],0,
                                       __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE) || expected==r[0]) {
            wait=native_mutex_waits+i; break;
        }
    }
    if (!wait) return -ENOMEM;
    if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_PSYNCH_MUTEXDROP) {
        int value=__atomic_load_n(&wait->credits,__ATOMIC_ACQUIRE);
        do {
            if (value==INT32_MAX) return -EOVERFLOW;
        } while (!__atomic_compare_exchange_n(&wait->credits,&value,value+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
        long result=RAW(SYS_futex,&wait->credits,FUTEX_WAKE_PRIVATE,1,0,0,0);
        return result<0 ? result : 0;
    }
    for (;;) {
        int value=__atomic_load_n(&wait->credits,__ATOMIC_ACQUIRE);
        if (value>0) {
            if (__atomic_compare_exchange_n(&wait->credits,&value,value-1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
            continue;
        }
        long result=RAW(SYS_futex,&wait->credits,FUTEX_WAIT_PRIVATE,0,0,0,0);
        if (result && result!=-EAGAIN) return result;
    }
}
