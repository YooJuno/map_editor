#!/usr/bin/env sh
set -eu
mapwright_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -s)" = Darwin ]; then
    mapwright_binary="$mapwright_dir/build/mapwright.app/Contents/MacOS/mapwright"
else
    mapwright_binary="$mapwright_dir/build/mapwright"
fi
if [ ! -x "$mapwright_binary" ]; then
    cmake -S "$mapwright_dir" -B "$mapwright_dir/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$mapwright_dir/build" --parallel
fi
exec "$mapwright_binary" "$@"
