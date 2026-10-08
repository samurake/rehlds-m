# Testing and qualification

## Release checks

| Lane | Coverage |
| --- | --- |
| GCC 15 i386 Release | Launcher regressions, synthetic pacing/output benchmark, ELF/dependency checks and packaging. |
| GCC 11 i386 Release | Supported compiler fallback and the same regression suite. |
| GCC 11 ASan/UBSan | Launcher memory/undefined-behavior checks with leak detection. |
| MSVC x86 Release | Windows regressions, CRT dependency checks and packaging. |
| Frozen GLIBC 2.34 | Tests and unwind probe using each extracted Linux archive; no GLIBC_2.35 backport. |
| Python helpers | Documentation links, release identity/build inputs and instrumentation validation. |

Run the [Linux profiles](../tools/linux/README.md) and
`tools/windows/build.cmd`. CI runs the same lanes before tag publication.
Offline checks are:

```sh
python -m pip install -r tools/linux/requirements-test.txt
python tools/release/check_docs.py
python -m unittest discover -s tools/release/tests -v
python -m unittest discover -s tools/linux/tests -v
```

Use Python 3.12 in an isolated environment for these host-side checks. NumPy is
used only by frame analysis and its regressions; it is not shipped to the server.

Regressions cover parsing/precedence, the 2000 ceiling, console EOF/escape,
completion cancellation, queue bounds, literal output, fmt i386 long doubles,
failed/full log storage, output backpressure, throwing workers, owned metrics
snapshots and lifetime stalls surviving ring rollover. Sanitizers cover launcher
tests, not the external engine or AMXX plugins.

## Engine integration

`tools/linux/qualify-startup.py` accepts an explicitly supplied copied fixture.
It checks executable-adjacent cfg lookup, invalid input before engine loading,
startup-only behavior, RCON version/case handling, command chaining, map reloads,
periodic metrics, pingboost's ceiling and clean shutdown. Disposable containers
use no external network and publish no ports.

The [load and lifecycle runners](../tools/linux/README.md) add scripted 10/32-player
combat at a 2000 target. Independent repetitions use fresh processes. The soak
keeps one process alive across windows/reloads, with engine-response heartbeats
and process/cgroup resource samples. Preserve hashes, settings, raw accepted-frame
timestamps and failed attempts; never reuse an evidence directory.

## Reading results

FPS counts accepted engine frames, not launcher iterations. CPU is the entire
server process as a percentage of one logical core. Launcher percentile reports
cover their latest 2048 iterations, not the whole gameplay measurement interval.
Lifetime counters also include map/population/probe-dump work; use aligned phase
evidence before attributing a pause. Heartbeat response timing is not network RTT.

Fake-client tests do not cover remote networking, external services or production
hosting. A passing short test is not proof of a memory plateau or months-long
stability. See [benchmarks](benchmarks.md) for measured comparisons and limitations.

## Local 1.4.0 release qualification

The release-preparation run stores evidence in `out/release-1.4.0-20261008/`.
The October 8 run used the current worktree based on `17a2332fca66`, portable
build settings and stable package label `v1.4.0`. Final builds had no compiler
warnings. Failed preparation attempts remain in the evidence directory; the
results below refer to the final `*-ready` builds and their extracted archives.

| Check | Result |
| --- | --- |
| GCC 15 Release / GCC 11 Release / GCC 11 ASan+UBSan | 144/144 tests in each lane. |
| MSVC x86 Release | 137/137 tests. |
| Frozen glibc 2.34, GCC 15 and GCC 11 archives | 145/145 checks per archive, including unwind and host preflight. |
| Isolated Python 3.12 helpers | 4 release + 9 instrumentation tests; documentation links passed. |
| Package integrity | Internal manifests/checksums passed: 30 Linux and 20 Windows files. |
| Engine startup integration | Cfg lookup/precedence, invalid input, startup-only settings, RCON version/case, chaining, metrics, three reloads and pingboost ceiling passed. |
| Repeated combat | Six fresh processes: three runs each at 10 and 32 players; 60 s warmup + 120 s measured. |
| Single-process combat soak | Fifteen 120 s windows at 32 players; 30 measured minutes, four reloads, fourteen 32-command bursts, 123 heartbeats and 396 resource samples. |

All combat tests used cfg target 2000, `sys_ticrate 0` and `fps_max 0`.

| Profile | Players | Median FPS [range] | CPU/core | Median p99 gap |
| --- | ---: | ---: | ---: | ---: |
| Fresh processes | 10 | 1990.7 [1979.4-1994.7] | 10.0% | 0.656 ms |
| Fresh processes | 32 | 1913.4 [1911.5-1915.4] | 16.8% | 1.447 ms |
| One-process soak windows | 32 | 1916.3 [1895.7-1932.7] | 16.7% | 1.405 ms |

Population/combat checks and clean shutdown passed. No server crash, AMXX runtime
error, probe overflow or container-local CPU throttling was detected. Recorded
output drops/write failures, report failures and input/completion rejections were
zero. The soak had no missing resource samples or OOM events. Measured frame gaps
never exceeded 50 ms; its largest measured gap was 13.020 ms. Its lifetime
launcher counter nevertheless recorded five iterations over 50 ms across all
phases, which must not be substituted for measured gameplay gaps.

Soak thread count stayed at 22 and descriptors at 41-42. RSS medians sampled
15-105 s into each gameplay window ranged from 179.0 to 183.6 MiB; the final
three were 181.2 MiB. This is descriptive evidence, not a long-term leak test.
The process high-water RSS in the probe report differs from periodically sampled
current RSS. Raw frames, phase events, telemetry and fixture hashes are retained
under `load-ready/`, `soak-ready/` and `startup-ready/` in the evidence directory.

Local archives use `--allow-dirty` and identify the uncommitted source as
`17a2332fca66-dirty`. Publication requires a reviewed clean-source commit/tag
and a fresh CI build; GitHub CI/publication were not run here. The Windows
launcher passed regressions and package checks, not a live Windows-engine test.
No production deployment or server restart was performed.
