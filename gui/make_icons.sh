#!/usr/bin/env bash
# Desktop icons for the five standalones — each one a picture of that pedal.
#
# The icon a user sees in their launcher should be the pedal they are about to open, so each one is
# that pedal's own face rather than a project logo: five applications with one icon between them is
# five identical entries in a menu.
#
# IT IS THE RENDERED FACE, NOT THE ART FILE. resources/img/pedal-<name>.png is a BLANK enclosure —
# the knobs, the legends, the lamp, the footswitch and the pedal's own name are all painted over it
# at run time by src/common/pedalface.cpp. Icons cut straight from that art are five empty coloured
# boxes, which is what the first version of this script produced and what it looked like in a real
# menu: the Boost and the Reverb are near enough the same green that only their names tell them
# apart, and neither had a name on it. So the icons come from tools/panelrender, which draws the
# same face the plug-in draws, using the same geometry and the same fonts, offline.
#
# The strip under the enclosure is cropped away: it belongs to the editor, not to the pedal, and an
# icon of a pedal should be a pedal. What is left is 468x691 with its own alpha edge, so it is
# trimmed to whatever is actually painted, scaled to fit the square, and centred on transparency.
# It is never stretched: a pedal is a tall object and squaring it by distortion would look like a
# mistake rather than a design.
#
# Regenerates packaging/icons/. Idempotent; safe to re-run after the art is re-exported.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build="${RPEDALS_BUILD_DIR:-$root/build}"
out="$root/packaging/icons"
sizes=(16 24 32 48 64 128 256)

command -v magick >/dev/null 2>&1 || {
    echo "make_icons: ImageMagick 7 (magick) is required" >&2
    exit 1
}
if [ ! -x "$build/panelrender" ]; then
    echo "make_icons: no panelrender at $build/panelrender — build the tree first:" >&2
    echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
    echo "or set RPEDALS_BUILD_DIR to a build directory." >&2
    exit 1
fi

faces=$(mktemp -d)
trap 'rm -rf "$faces"' EXIT

# Scale 1.0: the art's own size, so nothing is resampled before the icon resize below. The window
# panelrender draws is 468x761 — the enclosure, then the MIDI strip.
"$build/panelrender" --out "$faces" --resources "$root/resources" --scale 1.0 >/dev/null

mkdir -p "$out"
for pedal in boost chorus flanger delay reverb; do
    face="$faces/$pedal.png"
    [ -f "$face" ] || { echo "make_icons: panelrender wrote no $face" >&2; exit 1; }
    for size in "${sizes[@]}"; do
        magick "$face" \
            -crop 468x691+0+0 +repage \
            -background none -alpha set -trim +repage \
            -resize "${size}x${size}" \
            -gravity center -extent "${size}x${size}" \
            "$out/rations-$pedal-$size.png"
    done
    echo "make_icons: rations-$pedal-{$(IFS=,; echo "${sizes[*]}")}.png"
done
