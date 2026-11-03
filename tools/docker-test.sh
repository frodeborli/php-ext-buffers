#!/bin/sh
# Build the extension and the API consumer and run the phpt suite inside official php images (Debian = glibc, Alpine = musl).
# usage: tools/docker-test.sh [image ...]   default: the whole 8.4-8.5 x {bookworm, alpine} matrix plus ZTS images
set -u
src=$(cd "$(dirname "$0")/.." && pwd)
images=${*:-"php:8.4-cli-bookworm php:8.5-cli-bookworm php:8.4-cli-alpine php:8.5-cli-alpine
php:8.4-zts-bookworm php:8.5-zts-bookworm php:8.4-zts-alpine php:8.5-zts-alpine"}
status=0
for image in $images; do
  out=$(docker run --rm -v "$src":/src:ro "$image" sh -c '
    set -e
    if command -v apk >/dev/null; then apk add --no-cache $PHPIZE_DEPS >/dev/null; else apt-get update -qq >/dev/null && apt-get install -y -qq $PHPIZE_DEPS >/dev/null; fi
    /src/tools/build.sh /src /build "$(command -v phpize)" "$(command -v php-config)"
    php -v | head -1; php -r "echo PHP_ZTS ? \"ZTS\" : \"NTS\", \" \", php_uname(\"s\"), \"\n\";"; (ldd --version 2>&1 || true) | head -1
    cd /build
    NO_INTERACTION=1 REPORT_EXIT_STATUS=1 TEST_PHP_EXECUTABLE=$(command -v php) php run-tests.php -n -q -P --no-color --show-diff -d extension=/build/modules/buffers.so -d extension=/build/api/modules/buffers_api_test.so tests 2>&1 \
      | grep -E "^(FAIL|BORK|WARN)|Tests (passed|failed|skipped)|^[0-9]+[+-] "
  ' 2>&1)
  rc=$?
  printf '%s\n--- %s: exit %s\n' "$out" "$image" "$rc"
  [ $rc -eq 0 ] || status=1
done
exit $status
