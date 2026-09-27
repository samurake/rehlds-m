"""Validate build identity and package launcher runtimes; system libraries stay on the host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.check_output(
        ["git", "-c", f"safe.directory={ROOT}", "-C", str(ROOT), *args], text=True
    ).strip()


def identity(tag=None):
    version = re.search(r'project\("ReHLDS-M"\s+VERSION\s+"(\d+\.\d+\.\d+)"',
                        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    revision = git("rev-parse", "HEAD")
    if tag and not re.fullmatch(r"v" + re.escape(version) + r"(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", tag):
        raise ValueError(f"Tag {tag!r} does not match CMake version {version}")
    return {"version": version, "revision": revision,
            "label": tag or f"v{version}-{revision[:12]}",
            "prerelease": "true" if tag and "-" in tag else "false"}


def metadata():
    tag = os.environ.get("GITHUB_REF_NAME") if os.environ.get("GITHUB_REF_TYPE") == "tag" else None
    info = identity(tag)
    if not (ROOT / "docs" / "releases" / f"{info['version']}.md").is_file():
        raise ValueError("Missing release notes for the CMake version")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            for key, value in info.items():
                stream.write(f"{key}={value}\n")
    print(json.dumps(info, indent=2))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(args):
    info = identity(args.label)
    label = info["label"]
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    if dirty and not args.allow_dirty:
        raise ValueError("Refusing to release a build from modified tracked source")
    source = args.input.resolve()
    executable = source / ("hlds_linux" if args.platform == "linux-i386" else "hlds.exe")
    data = executable.read_bytes()
    if args.platform == "linux-i386":
        if data[:5] != b"\x7fELF\x01" or struct.unpack_from("<H", data, 18)[0] != 3:
            raise ValueError("Expected an i386 ELF launcher")
        manifest = json.loads((source / "artifact-manifest.json").read_text())
        if manifest.get("diagnostic") or manifest.get("max_allowed_glibc") != "2.34":
            raise ValueError("Release requires the non-sanitized GLIBC_2.34 compatibility gate")
        if {r["file"] for r in manifest["files"]} != {"hlds_linux", "libstdc++.so.6", "libgcc_s.so.1"}:
            raise ValueError("Incomplete Linux runtime manifest")
        for record in manifest["files"]:
            if record["file"] not in ("hlds_linux", "libstdc++.so.6", "libgcc_s.so.1"):
                raise ValueError("Unexpected runtime in the Linux manifest")
            if digest(source / record["file"]) != record["sha256"]:
                raise ValueError(f"Runtime hash mismatch: {record['file']}")
            if tuple(map(int, record["max_glibc"].split("."))) > (2, 34):
                raise ValueError("Release runtime exceeds the GLIBC_2.34 floor")
        required = ["hlds_linux", "libstdc++.so.6", "libgcc_s.so.1", "artifact-manifest.json",
                    "libgcc-package.txt", "libstdcpp-package.txt"]
        runtime_requirement = "GLIBC symbols through " + max(
            (r["max_glibc"] for r in manifest["files"]),
            key=lambda v: tuple(map(int, v.split("."))))
    else:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0x14C:
            raise ValueError("Expected an x86 PE launcher")
        manifest = json.loads((source / "artifact-manifest.json").read_text())
        required = [record["file"] for record in manifest["files"]]
        if "hlds.exe" not in required or "vcruntime140.dll" not in required or "msvcp140.dll" not in required:
            raise ValueError("Missing Windows CRT manifest entries")
        for record in manifest["files"]:
            if Path(record["file"]).name != record["file"] or not record["file"].endswith((".dll", ".exe")):
                raise ValueError("Unexpected Windows runtime filename")
            if digest(source / record["file"]) != record["sha256"]:
                raise ValueError(f"Runtime hash mismatch: {record['file']}")
        required += ["artifact-manifest.json", "MSVC-REDIST.txt"]
        runtime_requirement = "Windows 10/Server 2016 or later with Universal CRT; x86 VC++ runtime DLLs included"
    banner = subprocess.check_output([str(executable), "--version"], text=True).strip()
    expected = f"ReHLDS-M {info['version']} ({info['revision'][:12]}{'-dirty' if dirty else ''})"
    if banner != expected:
        raise ValueError(f"Binary/source identity mismatch: {banner!r}")
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"rehldsm-{label}-{args.platform}"
    with tempfile.TemporaryDirectory(prefix="rehldsm-package-") as temp:
        staging = Path(temp) / name
        staging.mkdir()
        for filename in required:
            shutil.copy2(source / filename, staging / filename)
        shutil.copy2(ROOT / "LICENSE", staging / "LICENSE")
        licenses = staging / "licenses"
        licenses.mkdir()
        for dependency in ("fmtlib", "spdlog", "stringpy"):
            matches = sorted((args.dependencies / f"{dependency}-src").glob("LICENSE*"))
            if not matches:
                raise ValueError(f"Missing license for {dependency}")
            for license_file in matches:
                if license_file.is_file():
                    shutil.copy2(license_file, licenses / f"{dependency}-{license_file.name}")
        if args.platform == "linux-i386":
            shutil.copy2(ROOT / "tools/linux/check-host.sh", staging / "check-host.sh")
            for prefix, directory in (("libstdcpp", Path("/usr/share/licenses/libgcc")),
                                      ("libgcc", Path("/opt/rehldsm-runtime/licenses"))):
                runtime_licenses = sorted(directory.glob("*"))
                if not runtime_licenses:
                    raise ValueError(f"Missing {prefix} runtime license notices")
                for license_file in runtime_licenses:
                    if license_file.is_file():
                        shutil.copy2(license_file, licenses / f"{prefix}-{license_file.name}")
        info.update(platform=args.platform, label=label, binary_banner=banner,
                    dirty=dirty, runtime_requirement=runtime_requirement,
                    repository="https://github.com/samurake/rehlds-m",
                    source_url=f"https://github.com/samurake/rehlds-m/tree/{info['revision']}")
        (staging / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        installation = ((ROOT / "tools/release/INSTALL-linux.md").read_text(encoding="utf-8")
                        if args.platform == "linux-i386" else
                        "Windows installation: stop the server, back up the existing launcher and CRT DLLs,\n"
                        "then copy hlds.exe and every included x86 CRT DLL into the server root together.\n"
                        "Windows supplies the Universal CRT. Start the server through its normal entry.\n")
        (staging / "README.txt").write_text(
            f"ReHLDS-M {label} - {args.platform}\n\n"
            f"Source: {info['source_url']}\nRequired: {runtime_requirement}\n"
            "CPU: SSE4.2. Launcher only; engine, game DLL and plugins are supplied separately.\n"
            f"\n{installation}\n"
            "Use --version to identify this launcher without loading the engine.\n"
            "Candidate builds have not been qualified with real player load on the hosting provider.\n",
            encoding="utf-8")
        checksums = [(p.relative_to(staging).as_posix(), digest(p)) for p in sorted(staging.rglob("*")) if p.is_file()]
        (staging / "SHA256SUMS.txt").write_text(
            "".join(f"{sha}  {name}\n" for name, sha in checksums), encoding="utf-8")
        if args.platform == "linux-i386":
            (staging / "hlds_linux").chmod(0o755)
            (staging / "check-host.sh").chmod(0o755)
            archive = args.output / f"{name}.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                for path in sorted(staging.iterdir()):
                    tar.add(path, arcname=path.name)
        else:
            archive = args.output / f"{name}.zip"
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zip_file:
                for path in sorted(staging.rglob("*")):
                    if path.is_file():
                        zip_file.write(path, path.relative_to(staging).as_posix())
    (args.output / f"{archive.name}.sha256").write_text(f"{digest(archive)}  {archive.name}\n", encoding="utf-8")
    print(f"Packaged {archive.name}: {banner}; {runtime_requirement}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("metadata")
    build = commands.add_parser("package")
    build.add_argument("--platform", choices=["linux-i386", "windows-x86"], required=True)
    build.add_argument("--input", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--dependencies", type=Path, required=True)
    build.add_argument("--label")
    build.add_argument("--allow-dirty", action="store_true", help="Local validation only; never used by CI")
    args = parser.parse_args()
    metadata() if args.command == "metadata" else package(args)
