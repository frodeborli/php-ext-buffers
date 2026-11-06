#!/bin/sh
# usage: tools/run-phpt.sh <php binary> <build dir> [run-tests.php flags]
# With an ASan build of PHP, pass --asan (run-tests checks each test for leaks; --XLEAK-- tests are exempt).
# Runs the phpt suite twice: with ext-buffers alone (tests that need the API consumer skip, and the
# tests of the behaviour without an operator handler run), then with the API consumer test extension
# loaded as well. Exit status 1 if either run fails.
php=$1 b=$2; shift 2
status=0
out=$(mktemp)
for exts in "-d extension=$b/modules/buffers.so" "-d extension=$b/modules/buffers.so -d extension=$b/api/modules/buffers_api_test.so"; do
  echo "--- extensions: $(echo "$exts" | sed 's#-d extension=[^ ]*/##g; s#\.so##g')"
  (cd "$b" && NO_INTERACTION=1 REPORT_EXIT_STATUS=1 TEST_PHP_EXECUTABLE=$php $php run-tests.php -n -q --no-color --show-diff $exts "$@" tests >"$out" 2>&1) || status=1
  grep -vE "^(PASS|SKIP) |^(PHP|INI actual|More .INIs|CWD|Extra dirs|VALGRIND|ZEND_VERSION|PHP_SAPI|PHP_OS|=====)" "$out" | grep -v "autoupdate\|libtool\|top level"
done
rm -f "$out"
exit $status
