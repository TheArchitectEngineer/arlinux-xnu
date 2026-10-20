# Wait-timer cancellation regression

Run on a Linux host with Ruby, Clang, pthreads and ASan/UBSan:

```sh
ruby tests/wait-timer.rb
ruby tests/wait-timer.rb 105d828633f770495727e344fbbcb99ae49f15cd
```

The candidate passes at O0 and O2. The upstream baseline fails the pending
timer cancellation assertion. The harness extracts the actual thread_unblock
and thread_timer_expire functions from the selected source; it does not use a
separate copied implementation of the change.

Covered cases:

- No timer: no cancellation or active-count decrement.
- Pending timer: cancellation succeeds and consumes its active count.
- Dequeued callback: cancellation returns false, retains the callback's count,
  but clears the set flag before the callback can acquire the thread lock.
- A subsequent untimed wait is not timed out by that old callback.
- A subsequent timed wait retains its own active count; the old callback does
  not consume its timeout, and its own callback eventually delivers it.
- Expiration clears the set flag before unblocking, avoiding recursive cancel.

The already-dequeued race uses a real pthread serialized by a mutex; timer
queue outcomes, thread structures and scheduler hooks are controlled adapters.
This does not execute the real timer queue, scheduler, Mach IPC or XPC path,
and is not an exhaustive concurrency proof.

The changed complete thread.c also compiled with the staged ARM64 server
recipe and candidate headers. No clean complete-server link or live server
runtime result is claimed. Earlier application crash-rate measurements from
the private branch are not used as evidence for this submission.
