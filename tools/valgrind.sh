#!/bin/sh
# Runs the whole phpt suite (including the API consumer tests) under valgrind with the Zend allocator off
# (USE_ZEND_ALLOC=0), so that every emalloc is a real malloc and any leak or invalid access - including
# parked pool objects at shutdown, foreign-tensor deleters and owner references - is reported. Any
# valgrind error fails the test.
# usage: tools/valgrind.sh [phpize-suffix]    (builds out of tree first, like tools/test.sh; needs valgrind)
set -e
ver=${1:-}
export BUILD=${BUILD:-/tmp/buffers-build-${ver:-default}}
bin=${PHP_BIN:-/usr/bin}
"$(dirname "$0")/build-local.sh" $ver >/dev/null
export VALGRIND_OPTS="${VALGRIND_OPTS:-} --suppressions=$(cd "$(dirname "$0")" && pwd)/valgrind.supp"
exec "$(dirname "$0")/run-phpt.sh" $bin/php$ver "$BUILD" -m
