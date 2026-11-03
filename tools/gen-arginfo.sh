#!/bin/sh
# Regenerates buffers_arginfo.h from buffers.stub.php with php-src's gen_stub.php (PHP_SRC=~/dev/php-src, 8.4 or newer).
set -e
cd "$(dirname "$0")/.."
php "${PHP_SRC:-$HOME/dev/php-src}/build/gen_stub.php" -f buffers.stub.php >/dev/null
