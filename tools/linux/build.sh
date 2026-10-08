#!/bin/sh
set -eu

case "${REHLDSM_GCC_TOOLSET:-11}" in
    11) ;;
    15) . /opt/rh/gcc-toolset-15/enable ;;
    *) echo 'Unsupported builder toolset' >&2; exit 2 ;;
esac

profile=${BUILD_PROFILE:-release}
if [ "$profile" = sanitize ] && [ "${REHLDSM_GCC_TOOLSET:-11}" != 11 ]; then
    echo 'The sanitizer profile requires the GCC_TOOLSET=11 builder and its matching i386 diagnostics' >&2
    exit 2
fi
jobs=${BUILD_JOBS:-4}
case "$jobs" in ''|*[!0-9]*|0) echo 'BUILD_JOBS must be a positive integer' >&2; exit 2 ;; esac
case "$profile" in
    release) build_type=Release; sanitize=OFF; benchmark=ON; build_dir=/work/build; artifact_dir=/output/linux-i386 ;;
    sanitize) build_type=RelWithDebInfo; sanitize=ON; benchmark=OFF; build_dir=/work/asan-build; artifact_dir=/output/asan-linux-i386 ;;
    *) echo "Unknown BUILD_PROFILE: $profile" >&2; exit 2 ;;
esac
mkdir -p /output

# /source is the checkout (read-only); /output is the local artifact directory.
# Runtime compatibility is checked from imported symbols, not the distro label.
{
    cat /etc/os-release
    g++ --version
    cmake --version
    ninja --version
    ldd --version
    rpm -qa | sort
} > /output/toolchain.txt
git -c safe.directory=/source -C /source rev-parse HEAD > /output/source-revision.txt
git -c safe.directory=/source -C /source status --porcelain > /output/source-status.txt
cmake -S /source -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DBUILD_UNIT_TESTS=ON \
    -DBUILD_BENCHMARKS="$benchmark" \
    -DENABLE_ASAN="$sanitize" -DENABLE_UBSAN="$sanitize" \
    -DENABLE_EXCEPTIONS=ON \
    -DOPTIMIZE_FOR_CURRENT_CPU=OFF \
    -DBIN_OUTPUT_DIR="$artifact_dir"
cmake --build "$build_dir" --parallel "$jobs"
cp /source/data/rehldsm.cfg "$artifact_dir/rehldsm.cfg"
python3 /source/tools/linux/build-provenance.py "$build_dir" "$artifact_dir/build-provenance.json"
# Use the maintained compatibility unwinder for both tests and deployment.
cp /opt/rehldsm-runtime/libgcc_s.so.1 "$artifact_dir/libgcc_s.so.1"
cp /opt/rehldsm-runtime/package.txt "$artifact_dir/libgcc-package.txt"
rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\nSource: %{SOURCERPM}\n' libstdc++.i686 > "$artifact_dir/libstdcpp-package.txt"
export LD_LIBRARY_PATH="$artifact_dir"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs" --output-junit /output/tests.xml
if [ "$profile" = release ]; then
    cp "$build_dir/frame_benchmark" /output/frame_benchmark
    "$build_dir/frame_benchmark" > /output/frame-benchmark.jsonl
    python3 /source/tools/linux/compat.py export "$build_dir" /output/compat-tests
fi
file "$artifact_dir/hlds_linux"
readelf -h "$artifact_dir/hlds_linux"
readelf -d "$artifact_dir/hlds_linux"
readelf --version-info "$artifact_dir/hlds_linux"
ldd "$artifact_dir/hlds_linux"
if [ "$profile" = release ]; then
    python3 /source/tools/linux/verify.py "$artifact_dir"
else
    python3 /source/tools/linux/verify.py "$artifact_dir" --diagnostic
fi
