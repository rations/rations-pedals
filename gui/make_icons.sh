#!/usr/bin/env bash
# Desktop icons for the five standalones, generated from the enclosure art itself.
#
# The icon a user sees in their launcher should be the pedal they are about to open, so each one
# is that pedal's own enclosure rather than a project logo: five applications with one icon
# between them is five identical entries in a menu.
#
# The art is 468x691 and already has its own alpha edge, so it is trimmed to whatever is actually
# painted, scaled to fit the square, and centred on transparency. It is never stretched: a pedal
# is a tall object and squaring it by distortion would look like a mistake rather than a design.
#
# Regenerates packaging/icons/. Idempotent; safe to re-run after the art is re-exported.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/resources/img"
out="$root/packaging/icons"
sizes=(16 24 32 48 64 128 256)

command -v magick >/dev/null 2>&1 || {
    echo "make_icons: ImageMagick 7 (magick) is required" >&2
    exit 1
}

mkdir -p "$out"
for pedal in boost chorus flanger delay reverb; do
    art="$src/pedal-$pedal.png"
    [ -f "$art" ] || { echo "make_icons: missing $art" >&2; exit 1; }
    for size in "${sizes[@]}"; do
        magick "$art" \
            -background none -alpha set -trim +repage \
            -resize "${size}x${size}" \
            -gravity center -extent "${size}x${size}" \
            "$out/rations-$pedal-$size.png"
    done
    echo "make_icons: rations-$pedal-{$(IFS=,; echo "${sizes[*]}")}.png"
done
