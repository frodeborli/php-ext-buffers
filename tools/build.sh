#!/bin/sh
# usage: tools/build.sh <source dir> <build dir> <phpize> <php-config> [extra configure args]
# Out-of-tree build (the source tree is copied) of the extension with the test helpers, and of the
# API consumer test extension (tests/api-consumer), which is compiled against the installed-layout
# header only: <ext/buffers/php_buffers_api.h>. Prints compiler warnings and errors; exit status 1 if any.
set -e
src=$1 dst=$2 phpize=$3 phpconfig=$4; shift 4
rm -rf "$dst" && mkdir -p "$dst" && cp -r "$src"/. "$dst"/ && rm -rf "$dst/.git"
jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
cflags="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
log=$dst/build.log
cd "$dst"
$phpize >/dev/null 2>&1
./configure --enable-buffers --enable-buffers-testing --with-php-config="$phpconfig" CFLAGS="$cflags" "$@" >/dev/null
make -j"$jobs" >"$log" 2>&1 || { cat "$log"; exit 1; }
# the header exactly as other extensions find it after "make install"
mkdir -p "$dst/inc/ext/buffers" && cp php_buffers_api.h dlpack.h "$dst/inc/ext/buffers/"
mkdir "$dst/api" && cp -r tests/api-consumer/. "$dst/api"/ && cd "$dst/api"
$phpize >/dev/null 2>&1
./configure --enable-buffers-api-test --with-php-config="$phpconfig" CFLAGS="$cflags -I$dst/inc" >/dev/null
make -j"$jobs" >>"$log" 2>&1 || { cat "$log"; exit 1; }
if grep -E "warning:|error:" "$log"; then exit 1; fi
echo "built $dst/modules/buffers.so and $dst/api/modules/buffers_api_test.so"
