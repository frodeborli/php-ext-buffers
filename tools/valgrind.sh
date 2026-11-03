#!/bin/sh
# Runs the whole phpt suite under valgrind with the Zend allocator off (USE_ZEND_ALLOC=0), so that
# every emalloc is a real malloc and any leak or invalid access - including parked pool objects at
# shutdown and foreign-tensor deleters - is reported. Any valgrind error fails the test.
# usage: tools/valgrind.sh [phpize-suffix]    (builds out of tree first, like tools/test.sh; needs valgrind)
set -e
ver=${1:-}
export BUILD=${BUILD:-/tmp/dlpack-build-${ver:-default}}
"$(dirname "$0")/build-local.sh" $ver >/dev/null
cd "$BUILD"
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 TEST_PHP_EXECUTABLE=$(command -v php$ver) \
  php$ver run-tests.php -n -q -m --no-color --show-diff -d extension_dir="$BUILD/modules" -d extension=dlpack.so tests 2>&1 \
  | grep -vE "^(PASS|SKIP) |^(PHP|INI actual|More .INIs|CWD|Extra dirs|ZEND_VERSION|PHP_SAPI|PHP_OS|=====)" | grep -v "autoupdate\|libtool\|top level"
