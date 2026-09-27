# Linux build and stability review — 2026-09-27

## Result and scope

The original checkout at `dadc00221bfac28e3e5ff15c64947575c49a3234` compiles for
32-bit Linux locally. All **90 existing tests passed**. The launcher also booted
the existing isolated ZP fixture on `zm_deko2`, accepted console commands, and
exited normally on `quit`: 14 Metamod plugins running, 17 AMXX modules correct,
204 AMXX plugins listed / 194 running. These counts describe the local fixture,
not today's live server configuration. The fixture has its own paused plugins.

No tracked C++ source or dependency pins were changed. Added the Linux builder,
artifact verifier, this review, and a Graphify map. Upgrade experiments and
failure reproductions were isolated in containers or ignored `out/` files.
Nothing was deployed to FTP and no live server was restarted.

ReHLDS-M is the **launcher and console/frame-loop layer**. It loads the engine
and plugins; it does not contain the custom weapon calculations, engine physics,
or resource precache tables. Improving this project can fix stalls and wasted
CPU, but cannot by itself remove an engine precache limit or parallelize AMXX
gameplay safely.

## Build evidence

| Check | Observed result |
| --- | --- |
| Compiler / build system | GCC 11.5.0, CMake 3.31.8, GNU Make, AlmaLinux 9 container |
| Target | ELF32 i386, C++17, Release/LTO, native CPU tuning disabled |
| Original dependencies | fmt 10.0.0, spdlog 1.12.0, StringPy 1.0.2, GoogleTest 1.13.0 |
| Unit tests | 90/90 passed |
| Executable | 466,292 bytes; `out/linux-review-20260927/linux-i386/hlds_linux` |
| SHA-256 | `6d9f09468c261f83112e0216a76dff68b1a41cd589ddc239eb72ce86c3a74799` |
| Direct shared libraries | libstdc++, libm, libc, libgcc_s; no Intel SVML/intlc dependency |
| Runtime smoke test | Clean startup and quit with ReHLDS 3.15.0.896 and ReGameDLL 5.30.0.814 |
| Runtime test boundary | Debian bookworm / glibc 2.36, network disabled, no connected players |

Build commands are in [tools/linux/README.md](../tools/linux/README.md).
The initial Debian bullseye builder failed on repository package 404s; switching
the toolchain environment required no project source changes.

**Deployment compatibility is not established by that build success.** The
launcher and packaged libstdc++ require GLIBC_2.34; the AlmaLinux libgcc requires
`_dl_find_object@GLIBC_2.35`. Its glibc reports 2.34 but backports that symbol. The
strict `--max-glibc 2.34` check correctly rejected this package. The prior host
audit recorded glibc 2.34, which alone cannot establish symbol availability.
Before a host trial, check its actual exported symbols or build/package against
a compatible older runtime, then repeat the loader and engine smoke tests. Do
not replace the host's glibc to solve launcher packaging. The launcher also needs
GLIBCXX_3.4.29; retaining an older libstdc++ providing only 3.4.28 is insufficient.

## Changes I would prioritize

### 1. Handle closed stdin and incomplete Escape input — high priority

[`console_input.cpp`](../libs/view/src/view/linux/console_input.cpp), lines 33–96,
polls stdin but ignores `POLLHUP`, `POLLERR`, and `POLLNVAL`. An EOF read becomes
`0`, and the loop immediately polls again. `handle_escape()` also performs
blocking reads for the next bytes without cancellation or a timeout.

Using the real `ConsoleInput` class with a pipe, process CPU went from **0.03% of
one core with stdin open to 100.00% after EOF**. Sending only Escape and asking
the input to stop hung until the harness's eight-second timeout.

Change the reader to distinguish data, EOF, EINTR and errors; stop polling a
closed stream while leaving the game loop running. Parse Escape sequences
incrementally. Add a wakeup descriptor or another explicit cancellation path so
`stop()` can interrupt waits. Avoid logging the same I/O failure every iteration.
This is the clearest measured CPU-saving opportunity when the host detaches its
console; it does not imply that the current live host is doing so.

### 2. Make shutdown and completion cancellable — high priority

[`InputPresenter::find_command_matches`](../libs/presenter/include/presenter/input_presenter.hpp),
lines 122–128, queues engine work then waits indefinitely in `future.get()`.
[`ConsoleInput::stop`](../libs/view/src/view/console_input.cpp), lines 116–120,
joins that input thread. The main function stops the server loop and shuts down
the engine before destroying the input object
([`hlds.main.cpp`](../apps/hlds/src/hlds/hlds.main.cpp), lines 51–54).

A harness with a stopped server loop, pending tab completion, and `stop()` did
not finish within eight seconds. This proves the missing cancellation behavior;
it is not a measurement of how frequently a real player/admin triggers the race.

Introduce an explicit shutdown state: reject/cancel new tasks, resolve pending
futures, wake and join console input, and then shut down the engine. Keep engine
command completion on the game thread. A timeout alone should not leave a task
capturing dangling presenter state or a borrowed string view.

### 3. Print console messages literally before upgrading fmt — high priority

[`ConsoleView::print`](../libs/view/include/view/linux/console_view.hpp), line 65,
calls `fmt::print(text)`. Already formatted engine/log output is interpreted as a
new format string. In the Release build, `engine output {literal}` lost the
braced portion; lone braces also truncated output. These cases did **not** crash
the original fmt 10 build in the experiment.

The same real header compiled with fmt 11.2.0 instead **aborted with SIGABRT and
`argument not found`**. Thus an otherwise successful dependency upgrade would
introduce a readily reproducible failure on this path.

Use `fmt::print("{}", text)` or a length-aware literal write. A scratch copy with
the one-line literal-format change preserved the exact text and exited normally
with both fmt 10.0.0 and 11.2.0. Apply it with a regression check before updating
the dependency. Also distinguish intentional formatting APIs from APIs that
accept externally supplied or already formatted messages.

### 4. Correct adaptive frame pacing — high priority for `-targetfps`

[`ServerLoop::run`](../libs/model/src/model/server_loop.cpp), lines 73–110,
starts its measurement **after** `run_frame()`. It adjusts for sleep overhead
and task/input processing but excludes the engine's work. The smoothing formula
at line 68 also divides integer nanosecond counts before calling `exp`, losing
the fractional ratio. A fixed 1.5% target increase cannot compensate for
variable engine load.

The actual `ServerLoop` with a fake engine, 1,000 requested FPS, and 1,000 frames:

| Injected engine work per frame | Measured loop FPS |
| --- | ---: |
| 0 microseconds | 1,015.53 |
| 250 microseconds | 809.71 |
| 750 microseconds | 575.62 |

These are synthetic local results, not a prediction of ZP FPS. They isolate a
logic error: all three workloads can fit within a one-millisecond frame budget,
but the loop adds its sleep after the engine work.

Schedule against monotonic absolute deadlines and include the entire frame.
Sleep only for the remaining budget; record and skip missed deadlines without an
unbounded catch-up loop. If retaining the average, convert the division to
floating point. Avoid an unrestricted spin/yield loop to inflate idle FPS.
This change affects `-targetfps`; it does not automatically improve the separate
`-pingboost 2` mode used in the earlier test-host audit.

### 5. Repair the queue contract and bound backlog — medium priority

[`ThreadSafeQueue`](../libs/util/include/util/threadsafe_queue.hpp), lines 350–380,
has blocking consumers but no notification in either `push` or `emplace`.
A consumer waiting before `push(42)` remained blocked until the eight-second
timeout. `front` and `back` also return references after releasing the lock.

Add correct wakeups, a closed/cancelled state where required, and a nonblocking
`try_pop` API for the frame loop. Move values out instead of copying them and
avoid returning unprotected references. Use a bounded per-frame processing
budget and explicit backlog limits. The current game loop checks `empty()` with
a single consumer, so the missing notification is not evidence that it normally
blocks during gameplay; the broken general queue contract is still worth fixing.

### 6. Reduce logging overhead and improve failure handling — measure next

[`logger.cpp`](../libs/util/src/util/logger.cpp), lines 55–57 and 104–106, builds
temporary strings and copies the formatted buffer again. Console output flushes
stdout on every write; `LogSettings::flush_on` defaults to `trace`, so file output
also flushes every enabled message. File output is conditional, not always on.

Write the completed buffer once, reduce intermediate allocations, and introduce
a deliberate flush policy that still preserves fatal/error diagnostics. Measure
console backpressure and disk stalls. Test unavailable/full log storage before
choosing recovery behavior: spdlog exceptions are disabled and its failure paths
can terminate the process. A bounded file-logging worker may help if profiling
shows disk waits; engine and AMXX work should stay on the game thread.

### 7. Harden and qualify release builds — medium priority

The CMake post-build runtime copy at
[`apps/hlds/CMakeLists.txt`](../apps/hlds/CMakeLists.txt), line 97, ignores copy
failures. The current CI uses a moving Linux runner baseline. Both can produce a
successful build whose package is unusable on the host. Add explicit dependency
closure/symbol checks and record source commit, compiler and dependency commits.
The new local verifier supplies a starting point for those packaging checks.

Release flags currently disable stack protection. Evaluate
`-fstack-protector-strong`, supported fortification, RELRO/NOW and non-executable
stack checks, retaining separate debug symbols for crash reports. Measure the
small overhead instead of assuming these should be disabled. Keep 32-bit ABI,
SSE4.2 baseline and native tuning off for portable host deployment. Sanitizer
checks belong in separate development configurations.

## Library update experiment

Release pages were checked on 2026-09-27. Tag-only experiments used a separate
copy of the same HEAD; original dependency files remain unchanged.

| Library | Checkout | Candidate / result |
| --- | --- | --- |
| fmt | 10.0.0 | [11.2.0](https://github.com/fmtlib/fmt/releases/tag/11.2.0) compiled; fix literal console output first |
| fmt newest checked | — | [12.2.0](https://github.com/fmtlib/fmt/releases/tag/12.2.0) failed in this GCC 11 i386 configuration |
| spdlog | 1.12.0 | [1.17.0](https://github.com/gabime/spdlog/releases/tag/v1.17.0) compiled with fmt 11.2.0 |
| GoogleTest | 1.13.0 | [1.18.0](https://github.com/google/googletest/releases/tag/v1.18.0) compiled; test-only, no server FPS benefit |
| StringPy | 1.0.2 | Keep [1.0.2](https://github.com/hun1er/stringpy/releases/tag/v1.0.2), latest release checked |

The fmt 11.2.0 + spdlog 1.17.0 + GoogleTest 1.18.0 combination passed **90/90**
existing tests. It is a viable migration candidate, not yet a qualified runtime
release: the braced-output abort shows why a passing old test suite is inadequate.

fmt 12.2.0 failed at `fmt/format.h:3108` with no `operator~` for its fallback
`fmt::v12::detail::uint128`. Investigate an upstream fix or a narrowly reviewed
backport before using it on i386. Do not change the server ABI to 64-bit to work
around a formatting-library build error. Library microbenchmarks do not predict
game-server FPS improvements.

## Validation and rollout order

1. Fix literal output, EOF handling and shutdown/task cancellation; add focused
   regression coverage for each reproduced failure.
2. Fix pacing and queue behavior; measure full-frame p50/p95/p99, missed deadlines,
   CPU and command latency under controlled engine workloads.
3. Update the qualified dependency combination, retain pinned commits and
   manifest verification, and repeat startup/restart/map-change checks.
4. Trial on the test server with the same engine, map, cvars and plugins as the
   comparison build, then realistic player/plugin load. Test disconnecting the
   hosting console, partial Escape, queued completion during quit, and log I/O
   failures. Keep an immediate rollback package.

The local startup test used no network and no players: Redis/SQL/HTTP service
integration, authentication, sustained gameplay, and live FPS remain unmeasured.
Prior host settings referenced above come from the earlier audit and were not
refreshed during this local review.

## Local evidence files

All are under `out/linux-review-20260927/` (ignored local artifacts):

- `build-baseline-multilib.log`: successful original build, 90 tests and ELF output.
- `linux-i386/artifact-manifest.json`, `SHA256SUMS.txt`: exact binary/runtime bytes.
- `runtime-startup.log`, `runtime-results.json`: isolated ZP startup and clean quit.
- `probes.cpp`, `run-probes.py`, `probes-results.json`: real-class failure/pacing harness.
- `format-results.json`, `format11-results.json`, `format-fixed-results.json`: formatting behavior before and after the scratch-only fix.
- `upgrade-build.log`: fmt 12.2.0 i386 failure.
- `upgrade-fmt11-build.log`, `upgrade-fmt11-tests.log`: successful compatible-version experiment.

The intentional hang probes were killed by a timeout. They were not run on a
live server. The generated architecture map is in `graphify-out/graph.html`.
