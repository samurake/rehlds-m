# Release procedure

The numeric version comes from `CMakeLists.txt`; notes live in
`docs/releases/<version>.md`. Version 1.4.0 uses the stable tag `v1.4.0`.

## Checks

GitHub Actions runs on branch pushes, pull requests, manual dispatch and version tags:

- GCC 15 Linux i386 Release; this is the published Linux package.
- GCC 11 Linux i386 Release fallback.
- GCC 11 Linux ASan/UBSan with leak detection.
- MSVC Windows x86 Release.
- Documentation links and offline qualification/release helper tests.

Both Linux release archives run the exported regression suite and unwind probe
on frozen GLIBC 2.34 without the GLIBC_2.35 backport. Only the GCC 15 and Windows
packages publish. See [testing](../../docs/testing.md) for local qualification.

`package.py` checks source/tag identity, tracked-source cleanliness, executable
architecture, version banner, runtime manifests/hashes, portable build provenance
and the build's copy of `rehldsm.cfg`. It stages an explicit file list with
licenses, checksums and installation instructions. Diagnostic/native builds are
not portable release packages.

`--allow-dirty` permits local packaging checks only; CI never uses it.
A dirty local package is not a substitute for the final clean-source CI artifacts.
Changing code after qualification requires rebuilding and repeating affected checks.

## Publishing

Commit the reviewed source and documentation, then confirm the branch build passes.
To publish the stable version:

```sh
git tag -a v1.4.0 -m "ReHLDS-M 1.4.0"
git push origin v1.4.0
```

These commands publish externally; run them only when release publication is intended.
A version suffix such as `-rc.2` produces a prerelease, not a stable release.

The tag triggers fresh builds. The publication job waits for every lane, verifies
downloaded archive hashes, creates a draft, uploads both platforms and publishes.
Published tags/releases are not replaced automatically. Failed uploads can be
retried while the release remains a draft.

`notes.py` combines the cumulative changelog with both installation guides.
It converts repository-relative documentation links to URLs pinned to the
released commit, so links work on the GitHub release page too.

Only the publication job has write permission. It uses GitHub's built-in token;
checkout does not persist credentials. No FTP or game-server credentials are needed.

## Package installation

Linux archives preserve executable permissions and bundle `libstdc++.so.6` and
`libgcc_s.so.1`, with a GLIBC_2.34 symbol ceiling. The host supplies glibc and
its matching loader. Run `check-host.sh` from the complete extracted archive
before installation; see [Linux installation](INSTALL-linux.md).

Windows archives contain only the launcher's transitive x86 Release CRT DLL
dependencies and redistribution notice, not the complete vendor CRT directory.
Windows supplies the Universal CRT and system DLLs. Replace the launcher and
bundled CRT files together while the server is stopped; see
[Windows installation](INSTALL-windows.md).

Source identity is in `build-info.json`, compiler/dependency identity in
`build-provenance.json`, and runtime requirements in `artifact-manifest.json`.
Packages exclude the engine, game DLL, Metamod and AMXX.
