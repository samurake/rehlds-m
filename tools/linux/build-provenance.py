"""Record resolved compiler and dependency commits in the distributable package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def create(build, output):
    cache = (build / "CMakeCache.txt").read_text()
    for expected in ("ENABLE_EXCEPTIONS:BOOL=ON", "OPTIMIZE_FOR_CURRENT_CPU:BOOL=OFF",
                     "SPDLOG_FMT_EXTERNAL:INTERNAL=ON", "SPDLOG_FMT_EXTERNAL_HO:INTERNAL=OFF",
                     "SPDLOG_NO_EXCEPTIONS:INTERNAL=OFF"):
        if expected not in cache.splitlines():
            raise RuntimeError(f"Release policy not satisfied: {expected}")
    compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", cache, re.M).group(1)
    dependencies = {}
    for name in ("fmtlib", "spdlog", "stringpy", "googletest"):
        source = build / "_deps" / f"{name}-src"
        if source.exists():
            dependencies[name] = subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if os.name == "nt":
        version = subprocess.run([compiler], capture_output=True, text=True)
        compiler_label = next(line for line in (version.stdout + version.stderr).splitlines()
                              if "Compiler Version" in line)
        compiler_package = None # The CRT manifest records the checked MSVC redistributable.
    else:
        compiler_label = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
        compiler_package = subprocess.check_output(
            ["rpm", "-qf", str(Path(compiler).resolve())], text=True).strip()
    report = {
        "compiler": compiler_label,
        "compiler_path": compiler,
        "compiler_package": compiler_package,
        "dependencies": dependencies,
        "dependency_patches": {"fmt-12.2-i386.patch": hashlib.sha256(
            (Path(__file__).resolve().parents[2] / "cmake/patches/fmt-12.2-i386.patch").read_bytes()).hexdigest()},
        "cmake_cache_sha256": hashlib.sha256((build / "CMakeCache.txt").read_bytes()).hexdigest(),
        "compile_commands_sha256": hashlib.sha256((build / "compile_commands.json").read_bytes()).hexdigest(),
        "cpu_baseline": "i386 / SSE4.2; no march=native",
        "exceptions": True,
        "fmt_linkage": "external compiled static fmt; shared by spdlog and launcher",
        "note": "Package identity, not a bit-for-bit reproducibility guarantee; RPM repositories update.",
    }
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    create(args.build, args.output)
