#!/bin/sh
# Read-only launcher preflight. Does not start the game engine or change the host.
set -eu
cd -- "$(dirname -- "$0")"
sha256sum --check --strict SHA256SUMS.txt
test -x ./hlds_linux || { echo 'hlds_linux is not executable; ask the host to retain mode 0755.' >&2; exit 1; }
test -e /lib/ld-linux.so.2 || { echo 'Host needs its distribution-provided 32-bit glibc loader.' >&2; exit 1; }
export LD_LIBRARY_PATH="$PWD"
export LD_BIND_NOW=1
./hlds_linux --version
echo 'Launcher dependencies loaded successfully. Engine, plugins and player load require separate testing.'
