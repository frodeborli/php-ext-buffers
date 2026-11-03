#!/bin/sh
# usage: tools/build-local.sh [phpize-suffix e.g. 8.4] [extra configure args]
# Out-of-tree build in $BUILD (default /tmp/buffers-build-<suffix>) with the test helpers, plus the API
# consumer test extension. PHP_BIN=/path/to/bin selects another PHP installation (debug, ZTS builds).
set -e
ver=${1:-}; [ $# -gt 0 ] && shift
bin=${PHP_BIN:-/usr/bin}
here=$(cd "$(dirname "$0")" && pwd)
"$here/build.sh" "$here/.." "${BUILD:-/tmp/buffers-build-${ver:-default}}" "$bin/phpize$ver" "$bin/php-config$ver" "$@"
