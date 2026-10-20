#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <assert.h>
#include <dlfcn.h>
static void* (*get_tsd_base)(void);
static pthread_key_t key;
static unsigned ready;
static void* bases[4];
static void* worker(void* argument) {
    uintptr_t index=(uintptr_t)argument;
    void* expected=get_tsd_base();
    bases[index]=expected;
    assert(pthread_setspecific(key,argument)==0);
    __atomic_fetch_add(&ready,1,__ATOMIC_SEQ_CST);
    while (__atomic_load_n(&ready,__ATOMIC_SEQ_CST)!=4) sched_yield();
    for (unsigned n=0;n<1000;++n) {
        uintptr_t actual;
        errno=E2BIG;
        __asm__ volatile(".inst 0x0000da09\nmov %0, x9" : "=r"(actual) :: "x9", "memory");
        assert(actual==(uintptr_t)expected);
        assert(pthread_getspecific(key)==argument);
        assert(errno==E2BIG);
    }
    return 0;
}
int main(void) {
    pthread_t threads[4];
    get_tsd_base=(void*(*)(void))dlsym(RTLD_DEFAULT,"sys_thread_get_tsd_base");
    assert(get_tsd_base);
    assert(pthread_key_create(&key,0)==0);
    for (uintptr_t i=0;i<4;++i) assert(pthread_create(&threads[i],0,worker,(void*)i)==0);
    for (unsigned i=0;i<4;++i) assert(pthread_join(threads[i],0)==0);
    for (unsigned i=0;i<4;++i) for(unsigned j=i+1;j<4;++j) assert(bases[i]!=bases[j]);
    assert(pthread_key_delete(key)==0);
    puts("PASS Darling guest per-thread TSD trap, pthread keys and errno");
    return 0;
}
