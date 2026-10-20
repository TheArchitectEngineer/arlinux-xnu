# Activity-ID checks

After configuring the Darling parent with this darlingserver revision, build
the `darlingserver_duct_tape` target, then run:

```sh
bash tests/run-activity-ids.sh /path/to/build/src/external/darlingserver
ruby tests/activity-rpc-numbers.rb 6738d0f
```

The native Linux test links the production generated trap wrapper, XNU voucher
allocator, and atomic implementation from the build. It checks initialization,
invalid counts (including signed negative input), starting IDs for varied-size
reservations, mock copyout failure propagation, and 8,000 concurrent reservations
from eight threads with no gaps or overlapping ranges. copyout is mocked and
panic aborts the test; no real client-memory access or server scheduling is tested.
Keep assertions enabled. Sanitizer-enabled production objects require matching
link flags and are not handled automatically by this small runner.

The Ruby check regenerates RPC definitions for this tree and a supplied baseline
revision, ensuring every existing numeric call ID is unchanged and only the new
activity-ID call is appended. It requires Python 3, Ruby, and baseline git history.
`6738d0f` was upstream main when this change was prepared; pass another pre-change
revision when rebasing.

End-to-end validation still requires the matching libsystem_kernel client change,
regenerated RPC sources, a rebuilt server, and a freshly started Darling namespace.
Exercise activity allocation from multiple Darwin processes, invalid output
pointers, and libdispatch voucher activity creation. This harness is not evidence
that those integration scenarios pass.
