"""Verify the GCC i386 launcher package. Run inside the Linux builder."""
import hashlib
import argparse
import json
from pathlib import Path
import re
import subprocess

RUNTIMES = ("hlds_linux", "libstdc++.so.6", "libgcc_s.so.1")
SYSTEM_LIBRARIES = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2",
                    "librt.so.1", "ld-linux.so.2"}
GLIBC_CEILING = (2, 34)


def read(*args):
    return subprocess.check_output(args, text=True)


def verify(directory, max_glibc=GLIBC_CEILING, diagnostic=False):
    records = []
    system_needed = set()
    for name in RUNTIMES:
        path = directory / name
        header = read("readelf", "-h", str(path))
        if "ELF32" not in header or "Intel 80386" not in header:
            raise RuntimeError(f"{name}: expected i386 ELF32")
        versions = read("readelf", "--version-info", str(path))
        glibc = sorted({tuple(map(int, v.split('.'))) for v in
                        re.findall(r"\bGLIBC_(\d+(?:\.\d+)+)\b", versions)})
        if not glibc or glibc[-1] > max_glibc or "GLIBC_PRIVATE" in versions:
            raise RuntimeError(f"{name}: incompatible glibc requirement: {glibc}")
        dynamic = read("readelf", "-d", str(path))
        needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
        missing = set(needed) - set(RUNTIMES) - SYSTEM_LIBRARIES
        if diagnostic:
            missing = {n for n in missing if not re.fullmatch(r"lib(?:asan|ubsan)\.so\.\d+", n)}
        if missing:
            raise RuntimeError(f"{name}: unbundled non-system dependencies: {sorted(missing)}")
        system_needed.update(set(needed) & SYSTEM_LIBRARIES)
        records.append({
            "file": name,
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "max_glibc": '.'.join(map(str, glibc[-1])),
            "needed": needed,
        })
    linked = read("ldd", "-r", str(directory / "hlds_linux"))
    if "not found" in linked or "undefined symbol" in linked:
        raise RuntimeError(linked)
    for name in ("libstdc++.so.6", "libgcc_s.so.1"):
        match = re.search(re.escape(name) + r"\s+=>\s+(\S+)", linked)
        if not match or Path(match[1]).resolve() != (directory / name).resolve():
            raise RuntimeError(f"{name}: ldd did not resolve the packaged runtime")
    (directory / "SHA256SUMS.txt").write_text(
        ''.join(f"{r['sha256']}  {r['file']}\n" for r in records), encoding="utf-8")
    report = {"architecture": "i386", "files": records, "diagnostic": diagnostic,
              "max_allowed_glibc": '.'.join(map(str, max_glibc)),
              "host_libraries": sorted(system_needed),
              "interpreter": "/lib/ld-linux.so.2",
              "ldd": linked,
              "scope": "Launcher and its GCC runtimes only; engine and plugins load separately."}
    (directory / "artifact-manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--max-glibc", default="2.34", help="Maximum allowed GLIBC symbol version (default: 2.34)")
    parser.add_argument("--diagnostic", action="store_true", help="Allow host sanitizer runtimes; not releasable")
    args = parser.parse_args()
    ceiling = tuple(map(int, args.max_glibc.split('.')))
    verify(args.directory.resolve(), ceiling, args.diagnostic)
