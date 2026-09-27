# Build and release workflow

The fork is `samurake/rehlds-m`; development for this release is on
`release/1.4.0`. `upstream` points to `hun1er/rehlds-m`, and `origin` points to the
fork. GitHub Actions builds Linux i386 Release, Linux ASan/UBSan and Windows x86
Release for every branch push, pull request and manual workflow dispatch.

Branch builds produce `packages-linux-i386`, `packages-windows-x86` and validation
artifacts. Download the package artifact from the completed workflow run and
extract the inner tar.gz or zip. The inner archives preserve the intended package
layout; the Linux tar preserves executable permission.

To publish a candidate after reviewing the branch build:

```sh
git tag -a v1.4.0-rc.1 -m "ReHLDS-M 1.4.0 release candidate 1"
git push origin v1.4.0-rc.1
```

The tag triggers fresh builds. Publication needs every build and sanitizer job
to pass. Version tags must match the numeric version in `CMakeLists.txt`; a
suffix such as `-rc.1` publishes a prerelease without marking it latest. A plain
numeric tag publishes a stable release. Update `docs/releases/<version>.md`
when increasing the CMake version. Tags are not moved after publication.

The release job verifies downloaded archive checksums, creates a draft, uploads
both platforms, then publishes it. Only this job has `contents: write`. It uses
the built-in `GITHUB_TOKEN`; no personal token or game-server credentials need
to be stored as secrets. A failed upload can be retried while the release is
still a draft. An already published release is never replaced automatically.

Actions are pinned to full commit hashes. Pull requests cannot enter the
publication job, and checkout does not persist credentials in the source tree.
The earlier automatic release-drafting workflow is replaced by this tested tag
publication flow to avoid conflicting version selection.

`package.py` checks executable architecture, source version/revision, tracked
source cleanliness, Linux runtime hashes and the standalone version output.
It packages only explicitly selected runtime files, manifests and notices.
Linux archives contain both C++ runtime libraries with RPM provenance and enforce
GLIBC_2.34 as the maximum imported symbol version. An extracted-archive test on
unbackported glibc 2.34 is required before publication. `check-host.sh` provides a
read-only host preflight. System glibc and its loader remain provider-managed.
Windows archives contain the unmodified x86 Release CRT DLLs from the installed
Visual Studio redistribution directory, its redistribution notice and a dependency
manifest. Unexpected non-system DLL dependencies fail the build. Windows itself
supplies system DLLs and the Universal CRT (Windows 10/Server 2016 or later).
`--allow-dirty` exists solely for local packaging validation and is never passed
by CI. Release/source information and dependency licenses accompany both archives.

Linux build details are in [../linux/README.md](../linux/README.md); the Windows
entry point is `tools/windows/build.cmd` and discovers Visual Studio 2022 through
`vswhere`. These are launcher builds. Game-server installation and host runtime
qualification remain separate from GitHub publication.
