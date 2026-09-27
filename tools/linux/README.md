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

Every release enforces a **GLIBC_2.34** symbol ceiling for the launcher,
`libstdc++.so.6`, and `libgcc_s.so.1`. The host supplies 32-bit glibc and
`/lib/ld-linux.so.2`; these must stay managed together by the hosting provider.
`GLIBC_2.35` names a symbol version, not a separately downloadable `.so` file.

RC1's AlmaLinux 9 libgcc imported `_dl_find_object@GLIBC_2.35`, even though that
distribution labels its backported glibc 2.34. The builder now uses the maintained
AlmaLinux 8 i686 libgcc ABI, retaining GCC 11's compiler and libstdc++ (which supplies
GLIBCXX_3.4.29). Both inspected unwinders export the same 141 versioned symbols.
The selected runtime is used for the regression/sanitizer tests as well as the
release. RPM version and source package names are included alongside its licenses.
No library binaries or symbol versions are patched. Repositories remain updateable;
future incompatible changes fail the dependency and baseline test gates.

The strict deployment gate is enabled by default (and can be lowered explicitly):

```powershell
docker run --rm --network none `
  --mount "type=bind,source=$repo,target=/source,readonly" `
  --mount "type=bind,source=$output,target=/output" `
  rehldsm-linux-builder python3 /source/tools/linux/verify.py `
  /output/linux-i386 --max-glibc 2.34
```

Successful verification writes manifests. It rejects missing non-system libraries,
unresolved relocations, wrong architectures and newer glibc requirements. Sanitizer
builds explicitly allow diagnostic runtimes and cannot be packaged for release.

CI extracts the actual release archive in the pinned `compat.Dockerfile` environment,
checks that its libc lacks GLIBC_2.35, runs the host preflight, and reruns the release
regression suite with immediate symbol binding and the packaged runtimes. This frozen
OS is only a test fixture, never a shipped dependency or a production OS recommendation.

On the host, extract the package into a staging directory and run `sh ./check-host.sh`.
This checks hashes, executable permissions, the 32-bit loader and launcher dependency
resolution without starting the game engine. A passing result does not qualify
separately loaded engines/plugins or real-player performance.

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
