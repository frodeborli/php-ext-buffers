#!/bin/sh
# usage: tools/oracle/run.sh decode|encode|doubles [format ...]
set -e
here=$(cd "$(dirname "$0")" && pwd)
cc -O2 -shared -fPIC -o "$here/codec_oracle.so" "$here/codec_oracle.c" -lm
cd "$(mktemp -d)"   # a clean directory: a stray opcode.py etc. would shadow the stdlib
exec python3 "$here/oracle.py" "$@"
