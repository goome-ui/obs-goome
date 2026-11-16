#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mode=${1:-check}
out=${OUT:-build-tests}
cc=${CC:-cc}
mkdir -p "$out"
flags="-std=c99 -O2 -Wall -Wextra -Werror -Itests/shim -Isrc -Inative"
[ -f "$out/goome.o" ] && [ "$out/goome.o" -nt native/goome.h ] || $cc -std=c99 -O2 -w -Inative -c src/goome-impl.c -o "$out/goome.o"
$cc $flags -c src/json.c -o "$out/json.o"
$cc $flags -c src/builder.c -o "$out/builder.o"
$cc $flags -c src/overlay.c -o "$out/overlay.o"
$cc $flags -c src/plugin-main.c -o "$out/plugin-main.o"
$cc $flags -c tests/shim/shim.c -o "$out/shim.o"
$cc $flags -c tests/harness.c -o "$out/harness.o"
libs="-lpthread"
case "$(uname -s)" in Linux*) libs="$libs -ldl" ;; MINGW*|MSYS*|CYGWIN*) libs="$libs -static" ;; esac
$cc "$out"/goome.o "$out"/json.o "$out"/builder.o "$out"/overlay.o "$out"/plugin-main.o "$out"/shim.o "$out"/harness.o -o "$out/harness" $libs
repo=$(pwd)
cd "$out"
./harness "$mode" "$repo"
