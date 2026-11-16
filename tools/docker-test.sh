#!/bin/sh
# Build the extension and the API consumer and run the phpt suite inside official php images (Debian = glibc, Alpine = musl).
# usage: tools/docker-test.sh [image ...]   default: the whole 8.3-8.5 x {bookworm, alpine} matrix plus ZTS images
# DOCKER_PLATFORM=linux/386 runs 32-bit PHP; any other platform that docker can run (arm64, s390x under QEMU) works the same way.
set -u
src=$(cd "$(dirname "$0")/.." && pwd)
images=${*:-"php:8.3-cli-bookworm php:8.4-cli-bookworm php:8.5-cli-bookworm php:8.3-cli-alpine php:8.4-cli-alpine php:8.5-cli-alpine
php:8.4-zts-bookworm php:8.5-zts-bookworm php:8.4-zts-alpine php:8.5-zts-alpine"}
status=0
platform=${DOCKER_PLATFORM:-$(docker version -f '{{.Server.Os}}/{{.Server.Arch}}')}
for image in $images; do
  docker pull -q --platform "$platform" "$image" >/dev/null   # a local tag may hold another architecture
  out=$(docker run --rm --platform "$platform" -v "$src":/src:ro "$image" sh -c '
    set -e
    if command -v apk >/dev/null; then apk add --no-cache $PHPIZE_DEPS >/dev/null; else apt-get update -qq >/dev/null && apt-get install -y -qq $PHPIZE_DEPS >/dev/null; fi
    /src/tools/build.sh /src /build "$(command -v phpize)" "$(command -v php-config)"
    php -v | head -1; php -r "echo PHP_ZTS ? \"ZTS\" : \"NTS\", \" \", php_uname(\"s\"), \"\n\";"; (ldd --version 2>&1 || true) | head -1
    cd /build
    /src/tools/run-phpt.sh "$(command -v php)" /build -P | grep -E "^(FAIL|BORK|WARN)|Tests (passed|failed|skipped)|^--- ext"
  ' 2>&1)
  rc=$?
  echo "$out" | grep -q '^FAIL' && rc=1
  printf '%s\n--- %s: exit %s\n' "$out" "$image" "$rc"
  [ $rc -eq 0 ] || status=1
done
exit $status
