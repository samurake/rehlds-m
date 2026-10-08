# Launcher internals

The launcher loads the existing engine and runs its dedicated-server API.
It does not parallelize the engine, game DLL or AMXX. Build and runtime
configuration are separate: compiler flags never come from `rehldsm.cfg`.

## Ownership

| Component | Owner and responsibility |
| --- | --- |
| `Core::StartupSettings` | Parsed before engine initialization; fixed for the process lifetime. |
| `Model::ServerLoop` | Engine thread; engine calls, console-command submission, completion and status. |
| `View::ConsoleInput` | Input thread; editing and bounded requests to the loop. |
| `Model::FrameMetrics` | Engine-thread sampling; fixed ring and lifetime counters. |
| `Model::MetricsReporter` | One worker; owned snapshots, percentile calculation and report output. |
| `Util::AsyncLogSink` | One worker; owned payloads and output destinations. |

Workers must not call engine/entity APIs or retain borrowed presenter/view data.
Queue locks protect metadata and copies; formatting and device I/O occur outside them.

## Startup and shutdown

[Main](../apps/hlds/src/hlds/hlds.main.cpp) handles `--version` before cfg parsing.
The cfg parser resolves the executable directory, reads at most 64 KiB plus one
overflow-detection byte, validates settings and removes launcher-only arguments.
`--check-config` returns before logger or engine initialization.

`Runtime` owns command registration, the engine session, loop, presenters and
input in destruction order. Shutdown closes the loop, cancels completion and
stops input before destroying presenters. The engine session shuts down while
launcher-command callback objects still exist. The outer log session closes last.

Output/report workers capture shared state rather than `this`. Bounded shutdown
may detach a worker blocked in device I/O; that state owns its destinations until
the worker exits. Exception boundaries contain optional worker failures, not
signals, memory corruption or exceptions crossing the engine ABI.

## Frame loop

[FramePacer](../libs/model/include/model/frame_clock.hpp) uses monotonic absolute
deadlines. The loop includes engine, input, completion, status and report
submission in its before-sleep budget. Overruns skip expired deadlines; there
is no unlimited catch-up loop. Legacy pingboost policies retain a 500 us floor.

Input is limited to 1022 bytes/line, 128 queued commands and four submissions per
iteration. Standalone quit has a reserved slot. Completion has 16 queue slots,
a 250 ms timeout and cancellation; EOF disables console reading without quitting.

[FrameMetrics](../libs/model/include/model/frame_metrics.hpp) has no per-sample
allocation, locks or extra clocks for the four stall counters.
[MetricsReporter](../libs/model/src/model/metrics_reporter.cpp) uses a two-slot
try-lock queue. Rejected submissions do not reset producer counters. Snapshot
sorting runs only on the worker; reported scopes are defined in the
[configuration reference](startup-configuration.md).

## Output and dependencies

[AsyncLogSink](../libs/util/src/util/async_log_sink.cpp) separates enqueue,
worker consumption and destination writes/flushes. A failed destination is
disabled; other destinations continue. Output is bounded and best-effort.

fmt 12.2.0 is a pinned compiled static dependency shared with spdlog 1.17.0.
The [i386 patch](../cmake/patches/fmt-12.2-i386.patch) fixes upstream long-double
formatting; its hash is recorded in build provenance. StringPy 1.0.2 and
GoogleTest 1.18.0 are pinned as well. GoogleTest is test-only.

Linux builds retain i386/SSE4.2, LTO, stack protection and a GLIBC_2.34 symbol
ceiling. Native CPU targeting is disabled in portable release builds.
The package includes the compatible C++ runtimes, never glibc or its loader.

## Changing this code

Keep engine calls on their owner thread, preserve bounded queues and keep
shutdown state independent of borrowed objects. Add a regression for a reproduced
failure before changing behavior. Re-run both compiler lanes, sanitizers,
Windows and extracted-package compatibility after changes; see [testing](testing.md).
