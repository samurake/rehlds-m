# ReHLDS-M

English | [Русский](README.ru.md)

ReHLDS-M 1.4.0 replaces the HLDS launcher on Linux and Windows. This fork is
based on [hun1er/rehlds-m](https://github.com/hun1er/rehlds-m). The engine, game
DLL, Metamod and AMXX plugins remain separate.

[Downloads](https://github.com/samurake/rehlds-m/releases) ·
[Builds](https://github.com/samurake/rehlds-m/actions/workflows/build-and-test.yml)

## Requirements

- Linux: 32-bit glibc 2.34 or newer, `/lib/ld-linux.so.2`, and an SSE4.2 CPU.
- Windows: Windows 10 / Server 2016 or newer; x86 Visual C++ runtime DLLs are bundled.
- An existing HLDS/ReHLDS server installation.

## Installation

On Linux, install `hlds_linux`, `libstdc++.so.6` and `libgcc_s.so.1` in the
server root, beside `hlds_run` and the game directory. Keep the executable
permission on `hlds_linux`. Follow the [installation guide](tools/release/INSTALL-linux.md)
for package validation, backups and rollback.

On Windows, stop the server and back up its launcher and runtime DLLs. Copy
`hlds.exe` and every bundled CRT DLL into the server root together, then restart.

Install the cfg beside the executable, not in the game directory:

```text
<server-root>/
  hlds_linux          # Linux launcher
  libstdc++.so.6       # Linux bundled runtime
  libgcc_s.so.1        # Linux bundled runtime
  rehldsm.cfg         # Launcher settings (Linux or Windows)
  hlds_run            # Existing Linux wrapper; leave in place
  cstrike/            # Existing game directory; leave in place
```

Windows uses `<server-root>/hlds.exe` and the bundled x86 CRT DLLs in the same
directory. See the [Windows installation guide](tools/release/INSTALL-windows.md).
Preserve an existing customized cfg. Do not copy runtime libraries into
`cstrike/`, `addons/`, `/lib/`, `/usr/lib/` or Windows system directories.
Archives contain only the launcher, its required runtimes, cfg, installation
instructions, validation/provenance and licenses; no engine, plugins or test tools.

## Configuration

The [sample cfg](data/rehldsm.cfg) selects a 1500-iteration/s target, 60-second
metrics reports and asynchronous output. Targets are capped at 2000.
For target pacing, keep `sys_ticrate 0` and `fps_max 0` in the game's `server.cfg`.

Remove `-targetfps` from launch options before upgrading: target pacing is now
cfg-only. Set `target_fps = 0` to use `-pingboost` levels 0–5 instead; the same
2000-iteration/s ceiling applies. Actual engine FPS depends on engine and plugin work.

See [configuration and metrics](docs/startup-configuration.md) for defaults,
limits, precedence and output behavior.

| `rehldsm.cfg` key | Allowed values | Built-in default | Shipped sample | Purpose |
| --- | --- | --- | --- | --- |
| `target_fps` | 0–2000 | 0 | 1500 | Target launcher iterations/s; 0 selects capped pingboost. |
| `frame_stats_seconds` | 0–3600 | 0 | 60 | Periodic report interval in seconds; 0 disables automatic reports. |
| `async_output` | `true` / `false` | false | true | Move sink formatting and console/file I/O to a worker. |
| `output_queue_capacity` | 64–4096 | 1024 | 1024 | Bounded output slots; 1024 uses about 8 MiB. |
| `output_flush_ms` | 10–1000 | 100 | 100 | Normal async batch/flush interval in milliseconds. |

Example for the 2000-target profile used in the load tests:

```ini
target_fps = 2000
frame_stats_seconds = 60
async_output = true
output_queue_capacity = 1024
output_flush_ms = 100
```

All five settings load **only at process startup**; restart after editing.
Unknown/duplicate keys and out-of-range values fail startup. A missing cfg uses
built-in defaults; `frame_stats_seconds` overrides the optional `-framestats` flag.
Async output is bounded and best-effort: inspect drop/failure counters rather
than treating it as archival logging. Validate the cfg with `--check-config`.

## Commands

| Command | Use |
| --- | --- |
| `hlds_linux --version` / `hlds.exe --version` | Print version and source revision without loading the engine. |
| `--check-config` | Validate startup settings without loading the engine. |
| `rehldsm_version` | Print running launcher identity through console or RCON. |
| `rehldsm_stats` | Request a metrics report from launcher input; not an RCON command. |

Existing utility options include `-condebug` (launcher file logging),
`-conclearlog`, `-ignoresigint` and `-pidfile <filename>`.
Console input supports editing, completion and a 100-command history in
`input_history.txt`. EOF leaves the server running headless.

Run `rehldsm_version` in the server console or through RCON to confirm the
loaded launcher, for example `rcon rehldsm_version` from a connected client.

## Measured performance

An earlier matched campaign compared the classic launcher, published RC2 and
the cfg-only 1.4.0 development build on the same ReHLDS/ZP fixture. Each cell
used three fresh processes, 8 s warmup and 30 s measured at a requested 2000
rate. Values are run medians; CPU is the whole server as a percentage of one
logical core, not the launcher's CPU alone.

| Players | Launcher / pacing | Accepted engine FPS | CPU/core | p99 frame gap |
| ---: | --- | ---: | ---: | ---: |
| 10 | Classic: `sys_ticrate 2000` | 869.2 | 7.1% | 1.951 ms |
| 10 | RC2: `-targetfps 2000` | 1990.9 | 10.5% | 0.664 ms |
| 10 | 1.4.0 cfg build: `target_fps = 2000` | 1981.2 | 10.4% | 0.683 ms |
| 32 | Classic: `sys_ticrate 2000` | 813.2 | 13.2% | 3.050 ms |
| 32 | RC2: `-targetfps 2000` | 1904.0 | 17.5% | 1.672 ms |
| 32 | 1.4.0 cfg build: `target_fps = 2000` | 1919.9 | 17.0% | 1.411 ms |

That cfg build delivered about 2.28×/2.36× the classic launcher's accepted FPS
at 10/32 players, with higher total CPU use. It did not consistently beat RC2.
Both ReHLDS-M target profiles used `sys_ticrate 0` and `fps_max 0`.

The final source's separate pre-tag qualification used longer 60 s warmups and
120 s measurements: **1990.7 FPS at 10 players and 1913.4 at 32**, three fresh
processes each. A 30-minute measured 32-player soak passed four reloads, with
1916.3 median FPS. These later results are not a new paired comparison against
classic/RC2. See [benchmarks](docs/benchmarks.md) and [qualification](docs/testing.md)
for artifact provenance, ranges and limits. Scripted fake clients do not test
remote networking or prove production-host stability; target 2000 does not
guarantee 2000 accepted engine frames.

## Building

Use the [Linux builder](tools/linux/README.md) or `tools/windows/build.cmd`.
The release matrix covers GCC 15, GCC 11, GCC 11 ASan/UBSan and MSVC x86.

For a direct build, use CMake 3.21+ and a C++20 compiler with 32-bit support:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_UNIT_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

A direct build does not perform the release package's runtime compatibility checks.
Binaries default to `bin/<compiler>-<version>-<configuration>/`.

`OPTIMIZE_FOR_CURRENT_CPU=ON` selects native CPU instructions on supported
compiler paths. Such binaries require compatible target CPUs; building in a VM
or container targets the CPU exposed there, not a remote game server.
Portable release builders keep this option off and already use Release optimization
and LTO. Native targeting is not a guaranteed FPS improvement.

## Development

- [Implementation and thread ownership](docs/architecture.md)
- [Tests and qualification](docs/testing.md)
- [Measured comparisons](docs/benchmarks.md)
- [Release procedure](tools/release/README.md)
- [1.4.0 changes](docs/releases/1.4.0.md)

## License

See [LICENSE](LICENSE). Included Half-Life SDK code also carries Valve's license.
