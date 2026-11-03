#!/bin/sh
# usage: tools/test.sh [phpize-suffix]   - build out of tree (test helpers enabled) and run the phpt suite
set -e
ver=${1:-}
export BUILD=${BUILD:-/tmp/dlpack-build-${ver:-default}}
"$(dirname "$0")/build-local.sh" $ver
cd "$BUILD"
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 TEST_PHP_EXECUTABLE=$(command -v php$ver) \
  php$ver run-tests.php -n -q -P --no-color --show-diff -d extension_dir="$BUILD/modules" -d extension=dlpack.so ${TEST_PHP_ARGS:-} tests 2>&1 \
  | grep -vE "^(PASS|SKIP) |^(PHP|INI actual|More .INIs|CWD|Extra dirs|VALGRIND|ZEND_VERSION|PHP_SAPI|PHP_OS|=====)" | grep -v "autoupdate\|libtool\|top level"
