#!/bin/sh
# usage: tools/oracle/run-fuzz.sh [cases] [seed] [dlpack.so]   - NumPy differential fuzz (run from a clean directory)
here=$(cd "$(dirname "$0")" && pwd)
cd "$(mktemp -d)"
exec python3 "$here/numpy_fuzz.py" "$@"
