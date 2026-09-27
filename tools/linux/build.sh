#!/bin/sh
set -eu

profile=${BUILD_PROFILE:-release}
jobs=${BUILD_JOBS:-4}
case "$profile" in
    release) build_type=Release; sanitize=OFF; benchmark=ON; build_dir=/work/build; artifact_dir=/output/linux-i386 ;;
    sanitize) build_type=RelWithDebInfo; sanitize=ON; benchmark=OFF; build_dir=/work/asan-build; artifact_dir=/output/asan-linux-i386 ;;
    *) echo "Unknown BUILD_PROFILE: $profile" >&2; exit 2 ;;
esac
mkdir -p /output

# /source is the checkout (read-only); /output is the local artifact directory.
# AlmaLinux 9 reports glibc 2.34 but can backport newer symbol versions.
# Check artifact-manifest.json for the actual runtime requirements.
{
    cat /etc/os-release
    g++ --version
    cmake --version
    ldd --version
    rpm -qa | sort
} > /output/toolchain.txt
git -c safe.directory=/source -C /source rev-parse HEAD > /output/source-revision.txt
git -c safe.directory=/source -C /source status --porcelain > /output/source-status.txt
cmake -S /source -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DBUILD_UNIT_TESTS=ON \
    -DBUILD_BENCHMARKS="$benchmark" \
    -DENABLE_ASAN="$sanitize" -DENABLE_UBSAN="$sanitize" \
    -DOPTIMIZE_FOR_CURRENT_CPU=OFF \
    -DBIN_OUTPUT_DIR="$artifact_dir"
cmake --build "$build_dir" --parallel "$jobs"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs" --output-junit /output/tests.xml
if [ "$profile" = release ]; then
    "$build_dir/frame_benchmark" > /output/frame-benchmark.jsonl
fi
file "$artifact_dir/hlds_linux"
readelf -h "$artifact_dir/hlds_linux"
readelf -d "$artifact_dir/hlds_linux"
readelf --version-info "$artifact_dir/hlds_linux"
ldd "$artifact_dir/hlds_linux"
python3 /source/tools/linux/verify.py "$artifact_dir"
