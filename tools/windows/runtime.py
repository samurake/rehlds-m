"""Stage licensed x86 Release CRT files and reject unbundled DLL dependencies."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

SYSTEM_DLLS = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
               "ole32.dll", "oleaut32.dll", "ws2_32.dll", "winmm.dll", "ntdll.dll",
               "bcrypt.dll", "crypt32.dll", "rpcrt4.dll", "version.dll", "ucrtbase.dll"}


def inspect_dependencies(file):
    data = file.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0x14c:
        raise RuntimeError(f"Not an x86 PE file: {file.name}")
    imports = subprocess.check_output(["dumpbin", "/nologo", "/dependents", str(file)], text=True)
    needed = sorted(set(re.findall(r"^\s+([\w.-]+\.dll)\s*$", imports, re.MULTILINE | re.IGNORECASE)))
    if not needed:
        raise RuntimeError(f"Cannot inspect dependencies: {file.name}")
    return needed


def stage(destination):
    redist = Path(os.environ["VCToolsRedistDir"])
    crt = redist / "x86/Microsoft.VC143.CRT"
    libraries = {file.name.lower(): file for file in sorted(crt.glob("*.dll"))}
    if not libraries:
        raise RuntimeError("Missing redistributable x86 Release CRT")

    # Follow imports from the launcher, not every file in the vendor CRT folder.
    # Inspect transitive DLLs before copying; unknown dependencies still fail.
    pending, selected, host = [destination / "hlds.exe"], {}, set()
    for file in pending:
        if file.name.lower() in selected:
            continue
        needed = inspect_dependencies(file)
        selected[file.name.lower()] = (file, needed)
        for dll in needed:
            name = dll.lower()
            if name in libraries:
                if name not in selected:
                    pending.append(libraries[name])
            elif name in SYSTEM_DLLS or name.startswith(("api-ms-win-", "ext-ms-win-")):
                host.add(name)
            else:
                raise RuntimeError(f"Unbundled dependency: {file.name} -> {dll}")

    records = []
    for file, needed in selected.values():
        if file.parent != destination:
            shutil.copy2(file, destination / file.name)
        records.append({"file": file.name, "sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "needed": needed})
    notice = Path(os.environ["VSINSTALLDIR"]) / "Licenses/1033/Redist.txt"
    shutil.copy2(notice, destination / "MSVC-REDIST.txt")
    report = {"architecture": "x86", "files": records, "host_libraries": sorted(host),
              "crt_version": redist.name, "crt_source": f"VC/Redist/MSVC/{redist.name}/x86/Microsoft.VC143.CRT",
              "scope": "Launcher CRT only; Windows supplies system DLLs and the Universal CRT."}
    (destination / "artifact-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Bundled {len(records) - 1} required x86 CRT libraries from {redist.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=Path("out/ci/windows-i386"))
    stage(parser.parse_args().destination)
