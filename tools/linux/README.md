# Local Linux build from Windows

This builder compiles the current checkout as an i386 Linux launcher and runs its
CTest suite, including console, cancellation and frame-pacing regressions. It requires Docker Desktop with Linux containers. No FTP or
game server is contacted. Source is mounted read-only; build intermediates stay
inside the container.

From the repository root in PowerShell:

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

The output contains `linux-i386/hlds_linux`, its GCC C++ runtimes, SHA-256 and ELF
dependency manifests, `tests.xml`, `frame-benchmark.jsonl`, `toolchain.txt`,
`source-revision.txt` and `source-status.txt`. A dirty status means the revision
alone does not identify the complete source. CMake downloads dependencies pinned
to commits in `cmake/`. The builder fixes the base
image digest; RPM repositories remain updateable, so this is a repeatable build
workflow, not a promise of bit-for-bit reproducibility across future dates.

The two libstdc++ development architectures in the Dockerfile are intentional:
GCC's x86-64 multilib driver needs the x86-64 package's `32` header layout as well
as the i686 link libraries. Only installing the i686 development package failed
with `bits/c++config.h` missing.

## Runtime compatibility

The output is for **32-bit Linux**, with the project's existing SSE4.2 CPU
requirement. It uses C++20, Release, LTO, stack protection, and `OPTIMIZE_FOR_CURRENT_CPU=OFF`.
It does not bundle an engine, game DLL, AMXX, glibc, or loader.

**Check the artifact manifest before deployment.** AlmaLinux 9's glibc reports
2.34 but includes backported symbol versions. On 2026-09-27 the built launcher
and libstdc++ required at most GLIBC_2.34, while its copied libgcc required
`_dl_find_object@GLIBC_2.35`. A plain glibc 2.34 installation therefore cannot be
assumed compatible with the whole directory. The launcher also requires
GLIBCXX_3.4.29, provided by the packaged libstdc++.

An optional strict deployment gate can reject newer glibc symbol requirements:

```powershell
docker run --rm --network none `
  --mount "type=bind,source=$repo,target=/source,readonly" `
  --mount "type=bind,source=$output,target=/output" `
  rehldsm-linux-builder python3 /source/tools/linux/verify.py `
  /output/linux-i386 --max-glibc 2.34
```

Successful verification writes manifests. `verify.py` without `--max-glibc` records the
actual requirements and checks i386 files and local runtime resolution.

The September audit artifacts are under `out/linux-review-20260927/`. They are
local investigation builds, with the original runtime defects still present.
The updated build and qualification results are under `out/modernization-20260927/`.
See [the modernization report](../../docs/modernization-20260927.md) for the fixes
and the remaining host-compatibility and real-player qualification work.

## Sanitizers and CI

Use a separate output directory and add `-e BUILD_PROFILE=sanitize` to `docker run`
to build RelWithDebInfo with AddressSanitizer and UndefinedBehaviorSanitizer.
This profile runs the same tests with leak detection and halt-on-error enabled.
Sanitizer binaries are diagnostics, not deployment artifacts. `BUILD_JOBS` controls
build/test parallelism (default 4).

GitHub Actions runs both Linux profiles and retains their output, alongside the
Windows build/test job. Branch runs upload packages, and matching version tags
publish GitHub releases after all checks pass; see [the release workflow](../release/README.md).

The release profile builds `frame_benchmark` and records four simulated engine
loads at a 1000 FPS target. Its `cpp_new_calls` counts C++ `operator new` calls
during the measured launcher loop, not all C allocations or real engine/plugin
allocations. Run comparisons on the same CPU allocation with other work idle.
Wall-clock FPS values are measurements, not CI pass/fail thresholds.
