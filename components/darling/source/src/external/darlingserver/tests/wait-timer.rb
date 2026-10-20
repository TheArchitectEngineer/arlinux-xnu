#!/usr/bin/env ruby
# Exercise actual unblock/expiration functions with controlled timer outcomes.
require 'tmpdir'
require 'open3'
root=File.expand_path('..',__dir__)
source=if ARGV[0]
  output,status=Open3.capture2('git','-C',root,'show',"#{ARGV[0]}:duct-tape/src/thread.c")
  abort 'cannot read baseline' unless status.success?
  output
else
  File.read(File.join(root,'duct-tape/src/thread.c'))
end
unblock=source[/boolean_t thread_unblock\(.*?^\};/m] or abort 'unblock missing'
expire=source[/void\nthread_timer_expire\(.*?^\}/m] or abort 'expiration missing'
code=<<~'C'
  #include <assert.h>
  #include <stdbool.h>
  #include <stdio.h>
  #include <pthread.h>
  #include <stddef.h>
  #define TRUE 1
  #define FALSE 0
  #define __unused __attribute__((unused))
  enum { THREAD_WAITING=-1, THREAD_AWAKENED=0, THREAD_TIMED_OUT=1 };
  typedef int boolean_t, wait_result_t, spl_t;
  struct timer { bool pending; };
  struct thread { int wait_result, wait_timer_active; bool wait_timer_is_set; struct timer wait_timer; };
  typedef struct thread *thread_t;
  typedef struct { struct thread xnu_thread; void *context; } dtape_thread_t;
  static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
  static _Thread_local bool held;
  static int cancels,resumes,timeouts;
  static dtape_thread_t *dtape_thread_for_xnu_thread(thread_t t) { return (dtape_thread_t*)t; }
  static void thread_lock(thread_t t) { (void)t; assert(!held); assert(!pthread_mutex_lock(&lock)); held=true; }
  static void thread_unlock(thread_t t) { (void)t; assert(held); held=false; assert(!pthread_mutex_unlock(&lock)); }
  static void assert_thread_magic(thread_t t) { assert(t); }
  static spl_t splsched(void) { return 0; }
  static void splx(spl_t s) { (void)s; }
  static bool timer_call_cancel(struct timer *t) { assert(held); cancels++; bool pending=t->pending; t->pending=false; return pending; }
  static void resume(void *p) { (void)p; assert(held); resumes++; }
  static struct { void (*thread_resume)(void*); } hooks={resume}, *dtape_hooks=&hooks;
  boolean_t thread_unblock(thread_t,wait_result_t);
  static void clear_wait_internal(thread_t t,wait_result_t result) { assert(held); timeouts++; thread_unblock(t,result); }
C
code+=unblock+"\n"+expire+"\n"
code+=<<~'C'
  static pthread_barrier_t barrier;
  static void *racing_expiration(void *p) {
    pthread_barrier_wait(&barrier);
    thread_timer_expire(p,NULL);
    return NULL;
  }
  static void reset(dtape_thread_t *t) { *t=(dtape_thread_t){0}; cancels=resumes=timeouts=0; }
  int main(void) {
    dtape_thread_t t;
    reset(&t);
    thread_lock(&t.xnu_thread);
    assert(thread_unblock(&t.xnu_thread,THREAD_AWAKENED));
    assert(cancels==0 && resumes==1 && t.xnu_thread.wait_timer_active==0);
    thread_unlock(&t.xnu_thread);
    reset(&t);
    t.xnu_thread.wait_timer_is_set=true; t.xnu_thread.wait_timer_active=1; t.xnu_thread.wait_timer.pending=true;
    thread_lock(&t.xnu_thread);
    thread_unblock(&t.xnu_thread,THREAD_AWAKENED);
    assert(cancels==1 && !t.xnu_thread.wait_timer_is_set && !t.xnu_thread.wait_timer.pending);
    assert(t.xnu_thread.wait_timer_active==0 && t.xnu_thread.wait_result==THREAD_AWAKENED);
    thread_unlock(&t.xnu_thread);
    for (int new_wait=0;new_wait<2;new_wait++) {
      reset(&t);
      t.xnu_thread.wait_timer_is_set=true; t.xnu_thread.wait_timer_active=1;
      /* pending=false represents a callback already dequeued from its timer. */
      pthread_t callback;
      assert(!pthread_barrier_init(&barrier,NULL,2));
      thread_lock(&t.xnu_thread);
      assert(!pthread_create(&callback,NULL,racing_expiration,&t.xnu_thread));
      pthread_barrier_wait(&barrier);
      thread_unblock(&t.xnu_thread,THREAD_AWAKENED);
      assert(cancels==1 && t.xnu_thread.wait_timer_active==1 && !t.xnu_thread.wait_timer_is_set);
      t.xnu_thread.wait_result=THREAD_WAITING;
      if(new_wait) { t.xnu_thread.wait_timer_active++; t.xnu_thread.wait_timer_is_set=true; }
      thread_unlock(&t.xnu_thread);
      assert(!pthread_join(callback,NULL));
      assert(timeouts==0 && t.xnu_thread.wait_result==THREAD_WAITING);
      assert(t.xnu_thread.wait_timer_active==new_wait);
      if(new_wait) {
        thread_timer_expire(&t.xnu_thread,NULL);
        assert(timeouts==1 && t.xnu_thread.wait_result==THREAD_TIMED_OUT);
        assert(t.xnu_thread.wait_timer_active==0 && !t.xnu_thread.wait_timer_is_set);
        assert(cancels==1); /* Expiration clears the flag before unblocking. */
      }
      assert(!pthread_barrier_destroy(&barrier));
    }
    puts("PASS: pending/racing wait timer cancellation and subsequent waits");
    return 0;
  }
C
Dir.mktmpdir('wait-timer-') do |dir|
  path=File.join(dir,'probe.c')
  File.write(path,code)
  %w[-O0 -O2].each do |optimization|
    binary=File.join(dir,'probe')
    abort 'compile failed' unless system(ENV.fetch('CC','clang'),'-std=c11','-D_GNU_SOURCE',optimization,
      '-g','-fsanitize=address,undefined','-pthread',path,'-o',binary)
    abort 'regression failed' unless system(binary)
  end
end
