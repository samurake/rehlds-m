"""Export and run the real release regression suite on the frozen glibc baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile


def export(build, destination):
    suite = json.loads(subprocess.check_output(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"], text=True))
    destination.mkdir(parents=True, exist_ok=True)
    tests = []
    for test in suite["tests"]:
        command = test["command"]
        executable = Path(command[0])
        # The launcher must come from the actual release directory.
        if test["name"] != "launcher_version":
            shutil.copy2(executable, destination / executable.name)
        tests.append({"name": test["name"], "binary": executable.name, "args": command[1:]})
    smoke = Path(__file__).with_name("runtime-unwind.cpp")
    subprocess.run(["g++", "-m32", "-std=c++20", "-fPIC", "-shared", "-DREHLDSM_UNWIND_LIBRARY",
                    str(smoke), "-o", str(destination / "libruntime-unwind.so")], check=True)
    subprocess.run(["g++", "-m32", "-std=c++20", str(smoke), "-L" + str(destination),
                    "-lruntime-unwind", "-Wl,-rpath,$ORIGIN", "-pthread",
                    "-o", str(destination / "runtime-unwind")], check=True)
    subprocess.run([str(destination / "runtime-unwind")], check=True, timeout=30)
    tests.append({"name": "runtime_unwind", "binary": "runtime-unwind", "args": []})
    (destination / "tests.json").write_text(json.dumps(tests, indent=2))


def check(candidate, binaries):
    libc = subprocess.check_output(["readelf", "--version-info", "/lib/libc.so.6"], text=True)
    if "GLIBC_2.34" not in libc or "GLIBC_2.35" in libc:
        raise RuntimeError("Compatibility image is not the unbackported glibc 2.34 baseline")
    env = dict(os.environ, LD_LIBRARY_PATH=str(binaries), LD_BIND_NOW="1")
    subprocess.run(["sh", str(binaries / "check-host.sh")], env=env, check=True, timeout=30)
    tests_dir = candidate / "compat-tests"
    tests = json.loads((tests_dir / "tests.json").read_text())
    results = []
    for test in tests:
        folder = binaries if test["name"] == "launcher_version" else tests_dir
        result = subprocess.run([str(folder / test["binary"]), *test["args"]],
                                env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=30)
        results.append({"name": test["name"], "exit_code": result.returncode})
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(f"Compatibility regression: {test['name']}")
    report = {"glibc": "2.34", "has_glibc_2_35": False, "tests": results,
              "scope": "Release launcher and regression suite; engine/plugins are separate."}
    (candidate / "compatibility.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Passed {len(results)} release tests on glibc 2.34 without GLIBC_2.35")


def check_archive(candidate):
    archives = list((candidate / "packages").glob("*-linux-i386.tar.gz"))
    if len(archives) != 1:
        raise RuntimeError("Expected exactly one Linux release archive")
    archive = archives[0]
    expected = archive.with_name(archive.name + ".sha256").read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Release archive checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="rehldsm-compat-") as temp:
        with tarfile.open(archive, "r:gz") as tar:
            for member in tar.getmembers():
                if member.name.startswith("/") or ".." in Path(member.name).parts or not (member.isfile() or member.isdir()):
                    raise RuntimeError("Unexpected release archive entry")
            tar.extractall(temp, filter="data")
        check(candidate, Path(temp))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    copy = commands.add_parser("export")
    copy.add_argument("build", type=Path)
    copy.add_argument("destination", type=Path)
    run = commands.add_parser("check")
    run.add_argument("candidate", type=Path)
    args = parser.parse_args()
    export(args.build, args.destination) if args.command == "export" else check_archive(args.candidate)
