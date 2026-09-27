# Launcher modernization — 2026-09-27

This records the initial local qualification of the first modernization stage
against source revision `dadc00221bfac28e3e5ff15c64947575c49a3234`. Source changes
were uncommitted when these measurements were taken. The subsequent fork and
release pipeline are documented in [the release workflow](../tools/release/README.md).
These measurements qualify a local candidate, not a live-host release.
Engine, game DLL, Metamod, AMXX and weapon-plugin interfaces were not changed.

## Behavior and implementation

| Area | Change | Main files |
| --- | --- | --- |
| Pacing | Absolute monotonic deadlines include engine work in the frame budget; expired deadlines are skipped without an unbounded catch-up loop. Linux uses `clock_nanosleep` with `TIMER_ABSTIME`. | `model/frame_clock.hpp`, `model/server_loop.cpp` |
| Measurement | Fixed 2048-sample rings record launcher frame time, engine-call time and wake lateness; lifetime maximum and over-budget counters survive window rollover. | `model/frame_metrics.hpp` |
| Input queue | Fixed 128-command storage, 1022-byte line limit, four commands submitted per iteration; standalone `quit` bypasses a full queue. Embedded line breaks and NULs are rejected. | `util/threadsafe_queue.hpp`, `model/server_loop.*` |
| Completion | Typed, owned requests replace arbitrary task callbacks/futures. Queue capacity 16, one request processed per iteration, 250 ms timeout, stop-token cancellation, at most 256 returned matches. | `model/server_loop.*`, `presenter/input_presenter.hpp`, `model/console_commands.cpp` |
| Console lifetime | `std::jthread` plus explicit cancellation/join; Linux poll wake pipe, nonblocking escape parsing, EOF exits the reader without ending the server or spinning. | `view/console_input.*`, `view/linux/console_input.cpp` |
| Ownership | One runtime owns the loop, history, presenters and reader. Close queues, cancel/join the reader, destroy dependents, then shut down the engine. Observer registration is removed with its presenter. | `hlds.main.cpp`, `presenter/output_presenter.hpp` |
| Logging | Engine text is treated as data, preserving literal braces. Optional file-open/write/flush failure disables that sink once while console logging continues. Bounded formatted output and a 64 KiB file buffer reduce waste. | `util/logger.cpp`, `core/exports.cpp`, platform console views |
| History | Scratch storage belongs to the history instance; reject overlong entries and cap file loading at 128 KiB before allocating contents. | `model/userinput_history.*`, `util/file.*` |
| Build | C++20, commit-pinned dependencies, release stack protection on Linux, explicit runtime packaging failures, Docker build and sanitizer CI profiles. | root/CMake files, `tools/linux`, GitHub workflow |

The hot loop retains the existing main-thread engine API contract. It does not
call engine/entity/Pawn operations from worker threads. Shared ownership remains
only where there is a real shared lifetime, such as a completion request handed
between input and the game thread.

The existing pingboost modes remain available. The pacing change takes effect
with `-targetfps`; it does not silently change the default mode or game settings.

## Controls and bounded behavior

```text
-targetfps 1000 -framestats 10 +sys_ticrate 0
```

`-targetfps` accepts 1–10000; `-framestats` accepts 1–3600 seconds. Invalid values
produce an error instead of partially parsing or silently selecting a mode.
The command-line processor supplies `+sys_ticrate 0` for target-FPS mode, but
server configuration must not override it with a conflicting value.

Use `rehldsm_stats` in the launcher console for an immediate report. This is not
an engine command, so RCON will not find it. Periodic reports are opt-in and a
final report is emitted on orderly exit when enabled. Engine-reported FPS and
launcher iterations are labeled separately: one launcher call need not equal
one accepted engine simulation step.

Saturation rejects new normal commands without replacing older queued commands.
Oversized input is discarded as a whole line rather than executing a truncated
prefix. Shutdown closes queues and cancels waiting completion requests. Engine
calls already running are not preempted. A completion timeout may produce no
suggestions during a long map load; that is preferable to indefinitely holding
up console shutdown.

Log formatting limits the payload passed to sinks to 8192 bytes plus prefix and
truncation marker. This is not a universal allocation limit: formatting a large
caller-provided value can allocate before reaching a sink. File logging flushes
warnings/errors immediately and ordinary output on the next message after a
one-second interval, or on orderly close. There is no background flush timer.
Console and file output remain synchronous, so slow kernel I/O can still delay
the caller. File failure is recoverable; memory corruption and engine crashes
are not treated as safe-to-resume errors.

## Libraries and platform boundary

| Dependency | Selected version | Pin |
| --- | --- | --- |
| fmt | 11.2.0 | `40626af88bd7df9a5fb80be7b25ac85b122d6c21` |
| spdlog | 1.17.0 | `79524ddd08a4ec981b7fea76afd08ee05f83755d` |
| GoogleTest | 1.18.0 | `063de7e9578f82b369302001269680b4b1553359` |
| StringPy | 1.0.2, unchanged version | `b2c1aa10e6c4e1553823930f562d19aa08b88713` |

fmt 12.2.0 was investigated in the preceding audit and did not compile with the
selected GCC 11 i386 toolchain; it is not part of this candidate. The upgraded
fmt library made the existing literal-brace formatting bug fatal, so both
platform views and engine output forwarding were fixed before qualification.

The candidate retains i386 and the existing SSE4.2 CPU requirement. Internal
C++20 use does not convert the engine/plugin ABI to 64-bit. Linux output contains
the launcher and its GCC runtimes; it does not contain an engine or glibc.

**Host compatibility is not yet qualified.** The AlmaLinux 9 builder reports
glibc 2.34, but its bundled `libgcc_s.so.1` imports
`_dl_find_object@GLIBC_2.35`. A host reporting 2.34 is not sufficient evidence
that the package will run. The local Debian fixture provides compatible symbols.
`verify.py --max-glibc 2.34` is an intentionally strict gate and rejects this
package. A compatible older runtime/toolchain or a verified host symbol set is
needed before installation; copying a different glibc into the game folder is
not part of this change.

## Validation and measurements

Evidence is stored locally under `out/modernization-20260927/`. The preceding
baseline audit is under `out/linux-review-20260927/` and documented in
[`linux-build-review-20260927.md`](linux-build-review-20260927.md).

The regression suite covers EOF CPU spin, partial escape/shutdown, completion
cancellation and timeout, whole-line rejection, bounded queues and wakeups,
literal-brace output, optional log failure, strict numeric parsing, frame
deadlines and metric-window behavior. Sanitizers cover these launcher tests;
they do not instrument the external engine or AMXX plugins.

At a 1000 FPS target, a controlled one-CPU Docker benchmark of 1000 iterations per
case measured:

| Simulated engine work | Original launcher FPS | Updated launcher FPS |
| --- | ---: | ---: |
| 0 microseconds | 1015.53 | 999.92 |
| 250 microseconds | 809.71 | 999.91 |
| 750 microseconds | 575.62 | 999.92 |
| 1500 microseconds | not recorded | 666.38 |

Updated p99 frame time was approximately 1.018–1.026 ms for the first three
cases. At 1500 microseconds of work, throughput is bounded by that work; the
launcher cannot maintain 1000 FPS. The measured loops made zero counted C++
`operator new` calls. That excludes startup, C `malloc`, and real engine/plugin
allocations. These numbers are controlled launcher results, not a player-loaded
ZP performance promise. Final evidence: `frame-benchmark-final-single-cpu.jsonl`. The earlier run is also preserved in `frame-benchmark-single-cpu.jsonl`.

An isolated, network-disabled ZP fixture loads the candidate with the existing
engine, 17 correct modules and 204 plugins (194 running, 10 paused game modes).
Qualification includes five reloads of `zm_deko2`, module/plugin inspection,
literal-brace output, metrics and orderly quit. No players or persistence
backends are present. Map-load/backend-timeout stalls are retained in maximum
frame-time metrics instead of being hidden as steady-state performance.

Final checks:

- Linux i386 Release (GCC 11.5): **113/113 tests passed**.
- Linux i386 ASan + UBSan, including leak detection: **113/113 tests passed**.
- Windows x86 Release (MSVC 14.44): **107/107 tests passed**. The six additional
  Linux cases exercise POSIX console input and `/dev/full` behavior.
- Final ZP fixture: all module/plugin checks passed, literal braces preserved,
  five map reloads completed, normal exit code 0. Threads remained 20 and open
  descriptors 41 throughout. RSS ranged from 166044 to 167284 KiB, rising by
  1240 KiB from first to last sample; anonymous RSS rose by 728 KiB. This short
  run cannot distinguish retained allocations/caches from a continuing leak.
- Final fixture frame p99 reports were approximately 1.018–1.021 ms; maximum frame
  time reached 3.00 seconds across map transitions with unavailable backends.
  This is an idle-server compatibility check, separate from the controlled
  performance benchmark above and from a test with real players.
- ELF architecture, packaged-runtime resolution and SHA-256 verification passed.
  The GLIBC_2.35 runtime requirement remains explicitly recorded in the manifest.
- Docker builder construction, both Linux CI script profiles, local Windows
  build/tests, graph update and `git diff --check` passed. The remote GitHub
  workflow had not run at the time of this initial local qualification.

Final launcher hashes:

```text
Linux:   181e06f3a92b796d864d6a8f19537c2fe4dfa173082da214e027a468efd61279
Windows: 95b517c42db37dc4d94cdea2791512e9d43f6e85d97566bb61f7964ba612fddf
```

Evidence files include `build-final.log`, `build-windows.log`,
`ci-sanitize-final.log`, `tests.xml`, `ci-sanitize/tests.xml`,
`runtime-results.json`, `runtime-mapcycles.log`, and each output directory's
hash/dependency manifests. The Linux candidate is in `linux-i386/` and the
Windows candidate in `windows-i386/` beneath the evidence directory.

## Remaining qualification and next optimization stage

1. Match the complete runtime package to the host's loader/symbol set, then test
   that exact package on the test server with rollback artifacts available.
2. Compare equal maps, player counts and plugin configurations at a fixed FPS
   target. Record p95/p99, deadline overruns, CPU usage and gameplay latency.
3. Run a longer map rotation with real clients and functioning backend services;
   track RSS trends, descriptors, plugin errors and allocator profiles.
4. Use profiles to select engine or weapon-plugin hot spots. Spatial queries,
   entity scans and per-frame plugin forwards are separate projects from the
   launcher. Any later calculation workers must use copied inputs, bounded
   queues and main-thread result validation; this change adds no gameplay workers.

No CPU affinity, real-time scheduler privilege, busy-spin default, global custom
allocator or fast-math setting is introduced. Such changes need measurements
and host-specific validation before they become defaults.
