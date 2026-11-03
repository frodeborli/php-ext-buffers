#!/bin/sh
# usage: tools/test.sh [phpize-suffix]   - build out of tree (test helpers enabled) and run the phpt suite,
# including the API consumer tests. PHP_BIN=/path/to/bin selects another PHP installation.
set -e
ver=${1:-}
export BUILD=${BUILD:-/tmp/buffers-build-${ver:-default}}
bin=${PHP_BIN:-/usr/bin}
"$(dirname "$0")/build-local.sh" $ver
exec "$(dirname "$0")/run-phpt.sh" $bin/php$ver "$BUILD" -P ${TEST_PHP_ARGS:-}
