#!/bin/sh
# Compila y corre las pruebas de tracecore en el host, sin PlatformIO ni gtest instalado
# (usa el shim de tests/shim). Enlaza el Packet.cpp/Utils.cpp REALES del core de Meshcore
# con los mocks de test/mocks (spec 002, research R-2).
#
# Uso: ./run_tests.sh [suite ...]      p. ej. ./run_tests.sh test_interop_packet
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
if [ ! -f "$ROOT/src/Packet.cpp" ]; then
  echo "not inside the Meshcore repo: src/Packet.cpp missing" >&2
  exit 2
fi

CXX=${CXX:-c++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CORE="-I $ROOT/src -I $ROOT/test/mocks"
FLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter $CORE -I $HERE/tracecore/include -I $HERE/tests/shim -I $HERE/tests/support"

# Código del core y de tracecore: se compila una sola vez (en paralelo) y se enlaza en cada suite.
for f in Packet Utils; do
  $CXX -std=c++17 -w $CORE -c "$ROOT/src/$f.cpp" -o "$OUT/core_$f.o" &
done
for f in "$HERE"/tracecore/src/*.cpp; do
  $CXX $FLAGS -c "$f" -o "$OUT/tc_$(basename "$f" .cpp).o" &
done
for f in "$HERE"/tests/support/*.cpp; do   # RepeaterModel, Fixtures, Hex...
  [ -f "$f" ] && $CXX $FLAGS -c "$f" -o "$OUT/sup_$(basename "$f" .cpp).o" &
done
wait

export MESHCORE_ROOT="$ROOT" PINGER_DIR="$HERE"   # los usa test_upstream_guard
suites=${*:-$(cd "$HERE/tests" && ls -d test_*)}
for name in $suites; do
  d="$HERE/tests/$name"
  if [ ! -d "$d" ]; then echo "no such suite: $name" >&2; exit 2; fi
  # Cada suite compila y corre en paralelo; la salida se imprime en orden al final.
  ( $CXX $FLAGS "$d"/*.cpp "$OUT"/core_*.o "$OUT"/tc_*.o "$OUT"/sup_*.o -o "$OUT/$name" && "$OUT/$name" ) \
    > "$OUT/$name.log" 2>&1 && echo 0 > "$OUT/$name.rc" || echo 1 > "$OUT/$name.rc" &
done
wait

fail=0
for name in $suites; do
  echo "== $name"
  cat "$OUT/$name.log"
  [ "$(cat "$OUT/$name.rc")" = 0 ] || fail=1
done
exit $fail
