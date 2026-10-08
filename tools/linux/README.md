# Linux builder

Docker builds the read-only checkout for i386; intermediates stay in the container.
The default toolchain is GCC Toolset 15 with Ninja on the pinned EL9 base.
GCC 11 remains the release fallback and ASan/UBSan toolchain.

## Build and test

Run from the repository root in PowerShell:

```powershell
$repo = (Get-Location).Path
$output = Join-Path $repo 'out/linux-local'
New-Item -ItemType Directory -Force -Path $output | Out-Null
docker build -t rehldsm-linux-builder -f tools/linux/Dockerfile tools/linux
if ($LASTEXITCODE -ne 0) { throw 'Builder image failed' }
docker run --rm `
  --mount "type=bind,source=$repo,target=/source,readonly" `
  --mount "type=bind,source=$output,target=/output" `
  rehldsm-linux-builder sh /source/tools/linux/build.sh
if ($LASTEXITCODE -ne 0) { throw 'Build or verification failed' }
```

For GCC 11, build a separate image with `--build-arg GCC_TOOLSET=11`.
For sanitizers, use that image, a fresh output directory and
`-e BUILD_PROFILE=sanitize`. `BUILD_JOBS` controls build/test parallelism.

Release builds enable C++20, LTO and stack protection, with native CPU targeting
off. Outputs include the launcher, bundled C++ runtimes, documented cfg,
CTest XML, synthetic benchmark, dependency manifests and build provenance.
Pinned source dependencies and base images do not freeze RPM repositories;
the workflow does not promise bit-for-bit reproducibility across dates.

Both libstdc++ development architectures are required: the multilib driver uses
the x86-64 package's 32-bit header layout and the i686 link libraries.

## Runtime and packaging

The launcher and every bundled runtime must import no GLIBC version above 2.34.
The builder uses the compatible EL8 unwinder with the EL9 shared C++ runtime.
The compiler's nonshared support and dependency patch are recorded with licenses.

`verify.py` rejects wrong architecture, unresolved dependencies, unexpected
libraries and newer GLIBC requirements. Sanitizer manifests are diagnostic and
cannot be released. `compat.py` exports the tests and runs them against the
actual extracted archive on frozen, unbackported GLIBC 2.34.

For complete package validation and publication, follow
[the release procedure](../release/README.md). Glibc and its loader remain
host-managed; never copy them from the builder to a game server.

## Isolated player-load comparison

These scripts require an existing private fixture image, Docker, Python with
NumPy and an instrumentation directory containing `frame_probe_mm_i386.so`,
`player_load.amxx` and their source files. They do not download a server fixture.

Install the tested host-side dependency with
`python -m pip install -r tools/linux/requirements-test.txt` in a Python 3.12 environment.

Test only the newly built launcher:

```powershell
python tools/linux/compare-player-load.py `
  --variants candidate --candidate C:/bench/current/hlds_linux `
  --instrumentation C:/bench/instrumentation --config data/rehldsm.cfg `
  --output out/current-load --players 10 32 `
  --repeats 5 --seconds 120 --warmup 60
```

The runner's `candidate` selector names the launcher under test, including a
stable build. To compare three versions, also provide `--classic` and `--rc2`
and select `--variants classic rc2 candidate`.

Each independent run uses a fresh network-isolated container. The default is
logical CPU 2, 1 GiB RAM and no CPU quota. Pinning is not exclusive CPU reservation.
The test copy of the cfg selects 2000; the shipping sample remains 1500.
Classic uses `sys_ticrate 2000`, RC2 uses `-targetfps 2000`, and the current
launcher uses cfg-only pacing. The engine, game, plugins and shared runtimes match.

The probe measures accepted engine frames. Reports include FPS, process CPU,
p95/p99/p99.9, maximum gaps and counts above 5/10/50 ms. Gameplay checks cover
population, survival, weapons, movement, damage and simulation time.
Keep other builds and server tests idle during measurements.

## Single-process lifecycle test

```powershell
python tools/linux/soak-candidate.py `
  --candidate C:/bench/current/hlds_linux `
  --instrumentation C:/bench/instrumentation --config data/rehldsm.cfg `
  --output out/current-soak --players 32 --windows 15 `
  --seconds 120 --warmup 60 --map-every 3 --command-burst 32
```

The probe supports at most 120 seconds/window; 15 windows provide 30 measured
minutes plus warmups and reload work. All windows share one process and are
not independent repetitions. Five-second resource polling, engine-response
heartbeats and lifecycle markers add monitoring overhead; do not compare this
directly against unmonitored runs.

Missing resources/telemetry, counter resets, local throttling/OOM events,
gameplay errors, unexpected output/command failures and unclean exit fail the
campaign. Resource growth is descriptive, not automatically a leak. Parent
cgroup limits and host contention are not excluded by local counters.

Use fresh evidence directories and retain failed attempts. Share sanitized
reports, not raw logs or private fixture settings. Fake clients do not qualify
real-client networking, external services or long-term production stability.
[Measured results](../../docs/benchmarks.md) and [test coverage](../../docs/testing.md)
describe those boundaries.
