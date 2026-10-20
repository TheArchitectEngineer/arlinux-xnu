#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <assert.h>
#include <dlfcn.h>
static void* (*get_tsd_base)(void);
static pthread_key_t key;
static unsigned long offset;
static unsigned ready;
static void* bases[4];
static void* worker(void* argument) {
    uintptr_t index=(uintptr_t)argument;
    void* expected=get_tsd_base();
    bases[index]=expected;
    assert(*(void**)((uintptr_t)__builtin_thread_pointer()+offset)==expected);
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
    unsigned long (*get_offset)(void)=(unsigned long(*)(void))dlsym(RTLD_DEFAULT,"sys_thread_get_native_tsd_slot_offset");
    assert(get_offset); offset=get_offset();
    printf("native TSD slot offset=%lu\n",offset);
    assert(offset>=16 && offset<=32760 && !(offset&7));
    assert(*(void**)((uintptr_t)__builtin_thread_pointer()+offset)==get_tsd_base());
    assert(pthread_key_create(&key,0)==0);
    for (uintptr_t i=0;i<4;++i) assert(pthread_create(&threads[i],0,worker,(void*)i)==0);
    for (unsigned i=0;i<4;++i) assert(pthread_join(threads[i],0)==0);
    for (unsigned i=0;i<4;++i) for(unsigned j=i+1;j<4;++j) assert(bases[i]!=bases[j]);
    assert(pthread_key_delete(key)==0);
    puts("PASS Darling guest native TSD slot publication and trap agreement");
    return 0;
}
