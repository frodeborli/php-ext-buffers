#!/bin/sh
# Regenerates dlpack_arginfo.h from dlpack.stub.php with php-src's gen_stub.php (PHP_SRC=~/dev/php-src).
# gen_stub from a recent php-src emits ZEND_RAW_FENTRY with more arguments than PHP 8.2/8.3 accept,
# so the abstract interface methods are rewritten to ZEND_ABSTRACT_ME_WITH_FLAGS.
set -e
cd "$(dirname "$0")/.."
php "${PHP_SRC:-$HOME/dev/php-src}/build/gen_stub.php" -f dlpack.stub.php >/dev/null
sed -i -E 's/ZEND_RAW_FENTRY\("([A-Za-z]+)", NULL, (arginfo_[A-Za-z_]+), (ZEND_ACC_PUBLIC\|ZEND_ACC_ABSTRACT), NULL, NULL\)/ZEND_ABSTRACT_ME_WITH_FLAGS(BufferInterface, \1, \2, \3)/' dlpack_arginfo.h
grep -q RAW_FENTRY dlpack_arginfo.h && { echo "unexpected RAW_FENTRY left" >&2; exit 1; } || true
