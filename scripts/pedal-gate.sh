#!/bin/sh
# The gate. Runs the two offline proofs and fails on either.
#
#   pedalcheck   — does each pedal do what its panel says, and is its DSP still the DSP that was
#                  proved byte-identical to rations-amp's (tools/pedalcheck.cpp)
#   panelrender  — does the art still measure what pedalgeometry.h claims, and does every legend
#                  still fit the slot it is drawn in (tools/panelrender.cpp)
#
# Neither needs a host, an audio device or a display, so this runs anywhere the build does — which
# is the point: it is meant to be run before every commit that touches src/pedals, src/common or
# resources/img, and after every re-export of the enclosure art.
#
# Usage: scripts/pedal-gate.sh [build-dir]        (default: build)

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-${RPEDALS_BUILD_DIR:-$root/build}}

if [ ! -x "$build/pedalcheck" ] || [ ! -x "$build/panelrender" ]; then
    echo "pedal-gate: no tools in $build; build them first:" >&2
    echo "    cmake -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build $build" >&2
    exit 2
fi

status=0

echo "== pedalcheck =="
if ! "$build/pedalcheck"; then
    status=1
fi

echo
echo "== panelrender =="
# --out is deliberately omitted: the gate is about the audit, and writing five PNGs into the tree
# on every run is noise. scripts/makedist-windows.sh passes --out because it needs the pixels.
if ! "$build/panelrender" --resources "$root/resources"; then
    status=1
fi

echo
if [ "$status" -eq 0 ]; then
    echo "pedal-gate: PASS"
else
    echo "pedal-gate: FAIL" >&2
fi
exit "$status"
