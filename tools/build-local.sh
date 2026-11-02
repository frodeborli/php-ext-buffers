#!/bin/sh
# usage: tools/build-local.sh [phpize-suffix e.g. 8.4] [extra configure args]
# Out-of-tree build in $BUILD (default /tmp/dlpack-build-<suffix>), with the test helpers enabled.
set -e
ver=${1:-}; [ $# -gt 0 ] && shift
src=$(cd "$(dirname "$0")/.." && pwd)
dst=${BUILD:-/tmp/dlpack-build-${ver:-default}}
rm -rf "$dst" && mkdir -p "$dst" && cp -r "$src"/. "$dst"/ && rm -rf "$dst/.git"
cd "$dst"
phpize$ver >/dev/null && ./configure --enable-dlpack --enable-dlpack-testing --with-php-config=$(command -v php-config$ver) CFLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers" "$@" >/dev/null
make -j"$(nproc)" 2>&1 | grep -E "warning|error" || true
echo "built $dst/modules/dlpack.so"
