#!/bin/sh
# Compila y corre las pruebas de tracecore en el host (sin PlatformIO ni gtest instalado:
# usa el shim de tests/shim). Uso: ./run_tests.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=$(mktemp -d)
fail=0
for d in tests/test_*/; do
  name=$(basename "$d")
  $CXX -std=c++17 -Wall -Wextra -Wno-unused-parameter -I tracecore/include -I tests/shim \
    $d*.cpp tracecore/src/*.cpp -o "$OUT/$name"
  echo "== $name"
  "$OUT/$name" || fail=1
done
rm -rf "$OUT"
exit $fail
