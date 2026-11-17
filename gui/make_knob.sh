#!/usr/bin/env bash
# Rations pedals — the knob art, drawn from nothing (ImageMagick 7).
# Copyright (c) 2026 rations. MIT licence (see LICENSE).
#
# Writes dial.png, 256x256: a black pointer knob with a gold spine along its nose
# and a gold cap on its hub. The parent amp draws the same knob for its built-in
# pedalboard (its dial_pedal.png), cut heavier there because it is drawn at 40
# units against the 98 a pedal's own window draws it at.
#
# WHY THIS IS DRAWN RATHER THAN CROPPED FROM A PHOTOGRAPH, which is what this
# script used to do with dial-base.png. A dial is one bitmap rotated about its
# centre, so whatever is in the bitmap turns with it — and a photograph carries its
# lighting: a highlight on one side and shade on the other. Turned through 270
# degrees that reads as a knob that is darker on one side, and the side moves.
# Centring the crop fixed the orbit and could not fix that.
#
# So nothing here is lit from a direction. The body is shaded by its distance in
# from its own outline, which is a bevel the same width all the way round; the hub
# is shaded by radius alone. And the finished knob is averaged with its own mirror
# image and then CHECKED against it, pixel for pixel: a knob that is not exactly
# symmetric about its pointer fails the build rather than shipping.
#
# Not a disc, and it does not need to be: a rotating bitmap needs only to be
# centred on its pivot and symmetric about its pointer. The shape is what makes
# the setting readable from across a room, and on a saturated enclosure the black
# body is its own outline.
#
# A knob still has no drop shadow, for the reason it never had one: a shadow that
# rotates with the knob is worse than none.
#
# Each operator below was settled by running it and looking at the numbers, not by
# recollection — in particular: radial-gradient with gradient:radii is linear in
# radius and centred between the two middle pixels of an even canvas (its mirror
# image differs from it by nothing), and a three-image -composite does not confine
# an overlay that has no alpha of its own, which is why over() builds the alpha.
set -euo pipefail
cd "$(dirname "$0")"
source ./geometry.sh

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Drawn at four times the stored size and reduced, so every edge is antialiased
# by the reduction rather than by each operator's own idea of it.
S=$((DIAL_PX * 4))   # working size
H=$((S / 2))         # a radius of 1.0 in the tables below is H pixels
C="$((H - 1)).5"     # the centre in -draw coordinates, which count pixel CENTRES
# Golds: the amp's piping gold (#B88B4C) taken lighter and darker for a bevel,
# the same four the parent amp draws its dials with.
GOLD_HI="#F4DFA0"; GOLD_MID="#C99B4B"; GOLD_LO="#7C5622"; GOLD_DK="#3B280E"

px()  { awk -v v="$1" -v h="$H" 'BEGIN{printf "%.2f", v*h}'; }
# A point straight up from the centre at radius r, as a -draw y coordinate.
up()  { awk -v c="$C" -v r="$(px "$1")" 'BEGIN{print c-r}'; }

# ---- primitives --------------------------------------------------------------

# clut OUT "r:colour" ...   colour as a piecewise-linear function of 0..1.
# Two stops at the same position make a hard step.
clut() {
    local out="$1"; shift
    local L=4096 pr="" pc="" args=() st r c a b
    for st in "$@"; do
        r="${st%%:*}"; c="${st#*:}"
        if [ -n "$pr" ]; then
            a=$(awk -v v="$pr" -v L=$L 'BEGIN{printf "%d", v*L+0.5}')
            b=$(awk -v v="$r"  -v L=$L 'BEGIN{printf "%d", v*L+0.5}')
            if [ $((b - a)) -gt 0 ]; then
                args+=( \( -size "1x$((b - a))" "gradient:$pc-$c" -rotate -90 \) )
            fi
        fi
        pr="$r"; pc="$c"
    done
    magick "${args[@]}" +append -resize "${L}x1!" "$out"
}

# radial OUT "r:colour" ...   the whole canvas shaded by radius.
radial() {
    local out="$1"; shift
    clut "$tmp/_clut.png" "$@"
    magick -size ${S}x${S} -define gradient:radii=$H,$H radial-gradient:black-white \
        "$tmp/_clut.png" -clut "$out"
}

# ring OUT r0 r1   a greyscale mask, white between the two radii.
ring() {
    magick -size ${S}x${S} xc:black \
        -fill white -draw "circle $C,$C $C,$(up "$3")" \
        -fill black -draw "circle $C,$C $C,$(up "$2")" "$1"
}

# over BASE OVERLAY MASK   lay OVERLAY on BASE where the greyscale MASK is white.
over() {
    magick "$1" \( "$2" "$3" -alpha off -compose CopyOpacity -composite \) \
        -compose Over -composite "$1"
}

# bar OUT r0 r1 width colour   a pointer bar with rounded ends, up from the centre.
bar() {
    local w2; w2=$(awk -v w="$(px "$4")" 'BEGIN{print w/2}')
    magick -size ${S}x${S} xc:none -fill "$5" -draw "roundrectangle \
        $(awk -v c=$C -v w=$w2 'BEGIN{print c-w}'),$(up "$3") \
        $(awk -v c=$C -v w=$w2 'BEGIN{print c+w}'),$(up "$2") $w2,$w2" "PNG32:$1"
}

# inlay BASE POINTER   sink POINTER into BASE: a soft dark edge under it, then it.
inlay() {
    magick "$1" \( "$2" -channel A -morphology Dilate Disk:3 +channel -fill black -colorize 100 \
        -channel A -blur 0x3 -evaluate Multiply 0.8 +channel \) -compose Over -composite \
        "$2" -compose Over -composite "$1"
}

# finish WORK NAME   mirror-average, reduce to the stored size, and prove the result.
# Averaged with its mirror AFTER the reduction as well as before: the resize
# filter's own rounding is enough to leave a handful of pixels one step apart.
finish() {
    local out="$OUT_DIR/$2.png"
    magick "$1" \( +clone -flop \) -evaluate-sequence Mean \
        -resize ${DIAL_PX}x${DIAL_PX} \( +clone -flop \) -evaluate-sequence Mean \
        -strip -depth 8 "PNG32:$out"
    local size diff
    size="$(magick identify -format "%wx%h" "$out")"
    if [ "$size" != "${DIAL_PX}x${DIAL_PX}" ]; then
        echo "make_knob: $2.png is $size, but geometry.sh says ${DIAL_PX}x${DIAL_PX}." >&2
        exit 1
    fi
    diff="$(magick "$out" \( +clone -flop \) -metric AE -compare -format '%[distortion]' info:)"
    if [ "$diff" != "0" ]; then
        echo "make_knob: $2.png differs from its own mirror image in $diff pixels." >&2
        echo "           A dial that is not symmetric about its pointer looks lopsided as it turns." >&2
        exit 1
    fi
    magick identify "$out"
}

# ---- the pointer knob ---------------------------------------------------------
# A round hub with a long tapered nose and a short tail.
outline="$(awk -v c=$C -v h=$H 'BEGIN{
    printf "polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
    c-0.11*h,c-0.97*h, c+0.11*h,c-0.97*h, c+0.60*h,c-0.05*h, c+0.50*h,c+0.36*h,
    c+0.22*h,c+0.84*h, c-0.22*h,c+0.84*h, c-0.50*h,c+0.36*h, c-0.60*h,c-0.05*h}')"
# The corners are rounded by blurring the hard shape and tightening it again.
magick -size ${S}x${S} xc:black -fill white -draw "$outline" \
    -draw "circle $C,$C $C,$(up 0.66)" -blur 0x22 -level 46%,54% "$tmp/sil.png"
# Distance in from the outline, saturating at 90 working pixels, through a colour
# strip: outline first, so the edge is dark, the shoulder catches the light and the
# top is a satin black.
magick "$tmp/sil.png" -threshold 50% -morphology Distance Euclidean:4,'90!' \
    -blur 0x5 "$tmp/dist.png"
clut "$tmp/_shade.png" 0:#050505 0.15:#1C1C1C 0.38:#5A5A5D 0.62:#2A2A2B 1:#1C1C1D
magick "$tmp/dist.png" "$tmp/_shade.png" -clut "$tmp/b.png"
bar "$tmp/p.png" 0.32 0.90 0.06 "$GOLD_HI"
inlay "$tmp/b.png" "$tmp/p.png"
radial "$tmp/hub.png" 0:$GOLD_HI 0.10:$GOLD_MID 0.18:$GOLD_LO 0.205:$GOLD_DK \
    0.205:#000000 0.24:#000000 1:#000000
ring "$tmp/m.png" 0 0.24
over "$tmp/b.png" "$tmp/hub.png" "$tmp/m.png"
magick "$tmp/b.png" \( "$tmp/sil.png" -alpha copy \) -compose DstIn -composite "PNG32:$tmp/w.png"
finish "$tmp/w.png" dial
echo "Wrote dial.png (drawn, mirror-checked, no directional lighting)."
