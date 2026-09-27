"""Verify the GCC i386 launcher package. Run inside the Linux builder."""
import hashlib
import argparse
import json
from pathlib import Path
import re
import subprocess


def read(*args):
    return subprocess.check_output(args, text=True)


def verify(directory, max_glibc=None):
    records = []
    for name in ("hlds_linux", "libstdc++.so.6", "libgcc_s.so.1"):
        path = directory / name
        header = read("readelf", "-h", str(path))
        if "ELF32" not in header or "Intel 80386" not in header:
            raise RuntimeError(f"{name}: expected i386 ELF32")
        versions = read("readelf", "--version-info", str(path))
        glibc = sorted({tuple(map(int, v.split('.'))) for v in
                        re.findall(r"\bGLIBC_(\d+(?:\.\d+)+)\b", versions)})
        if not glibc or (max_glibc and glibc[-1] > max_glibc):
            raise RuntimeError(f"{name}: incompatible glibc requirement: {glibc}")
        dynamic = read("readelf", "-d", str(path))
        records.append({
            "file": name,
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "max_glibc": '.'.join(map(str, glibc[-1])),
            "needed": re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic),
        })
    linked = read("ldd", str(directory / "hlds_linux"))
    if "not found" in linked:
        raise RuntimeError(linked)
    for name in ("libstdc++.so.6", "libgcc_s.so.1"):
        match = re.search(re.escape(name) + r"\s+=>\s+(\S+)", linked)
        if not match or Path(match[1]).resolve() != (directory / name).resolve():
            raise RuntimeError(f"{name}: ldd did not resolve the packaged runtime")
    (directory / "SHA256SUMS.txt").write_text(
        ''.join(f"{r['sha256']}  {r['file']}\n" for r in records), encoding="utf-8")
    report = {"architecture": "i386", "files": records,
              "ldd": linked,
              "scope": "Launcher and its GCC runtimes only; engine and plugins load separately."}
    (directory / "artifact-manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--max-glibc", help="Optional deployment gate, e.g. 2.34")
    args = parser.parse_args()
    ceiling = tuple(map(int, args.max_glibc.split('.'))) if args.max_glibc else None
    verify(args.directory.resolve(), ceiling)
