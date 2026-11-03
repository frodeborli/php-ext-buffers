#!/bin/sh
# usage: benchmarks/run.sh [path/to/dlpack.so] [section]
# Runs bench.php twice - interpreter and tracing JIT - and prints both side by side.
# The JIT run is only reported if opcache's jit buffer shows that code was compiled.
# (opcache.file_update_protection=0: by default opcache refuses to compile files younger than 2 s, silently.)
set -e
here=$(cd "$(dirname "$0")" && pwd)
so=${1:-$here/../modules/dlpack.so}
section=${2:-all}
php=${PHP:-php}
opc=""; $php -n -m | grep -q "Zend OPcache" || opc="-d zend_extension=opcache"   # built in since PHP 8.5
common="-n -d memory_limit=-1 -d extension=$so $opc -d zend.assertions=-1 -d opcache.enable_cli=1 -d opcache.file_update_protection=0"
tmp=$(mktemp -d)
$php $common -d opcache.jit=off "$here/bench.php" "$section" > "$tmp/interp.tsv"
$php $common -d opcache.jit=tracing -d opcache.jit_buffer_size=128M "$here/bench.php" "$section" > "$tmp/jit.tsv"
echo "== $($php -r 'echo PHP_VERSION, PHP_ZTS ? " ZTS" : " NTS";'): $( [ "$section" = memory ] && echo "memory per element" || echo "ns per element access (best of N), lower is better")"
awk -F'\t' 'FNR==NR { i[$1]=$2; if (!($1 in order)) { order[$1]=++n; names[n]=$1 } next } { j[$1]=$2 } END {
  for (k=1;k<=n;k++) { nm=names[k]; if (nm ~ /^memory/) continue; printf "%-34s interpreter %8.1f   JIT %8.1f\n", nm, i[nm], j[nm] } }' "$tmp/interp.tsv" "$tmp/jit.tsv"
grep '^memory' "$tmp/jit.tsv" | awk -F'\t' '{ printf "%-34s %8.2f bytes/element\n", $2, $3 }' || true
rm -rf "$tmp"
