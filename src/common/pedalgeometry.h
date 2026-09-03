// Rations Pedals — where everything on a pedal goes.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// IN THE ART'S OWN COORDINATES, and that is the whole difference from rations-amp. There the five
// enclosures were blitted 2.46x down onto a shared board page and every number was expressed in
// that shrunken space; here one enclosure fills its own window at 1:1, so the numbers are the ones
// the art was measured in. Un-scaling the old layout rather than multiplying it by 2.4632 is what
// keeps a rounding error out of every single constant, and it is also what makes the extra room
// usable: a face 468 units wide fits one row of knobs where a face 190 wide needed two.
//
// EVERY NUMBER BELOW IS MEASURED OR DERIVED, and the measurements are re-taken by
// tools/panelrender on every build rather than trusted — the art can be re-exported, and a
// re-export that moves an edge should break the build rather than move a legend onto a border.
//
// WHAT WAS MEASURED (magick, all five enclosures, five scanlines each):
//   image            468 x 691
//   opaque body      x   35 .. 431   (x 1 .. 466 across the jack lugs, y ~300)
//   printable face   x   52 .. 414,  y 17 .. 674   — identical on all five, at every height
// The face is the coloured area inside the black border trim. The BODY is the right number for
// deciding where a patch cable would meet the box; the FACE is the only right number for deciding
// where lettering may go, and confusing the two is what once put a legend on the edge.
#pragma once

#include "pedalids.h"

#include "gfx/palette.h"

#include <cstdint>

namespace Rations
{
namespace geo
{

using namespace Rations::pal;

//--------------------------------------------------------------------------------------------
// The window. The enclosure at 1:1, and a strip beneath it for the one thing a real pedal has no
// place for: which MIDI message works its footswitch.
constexpr int kArtW = 468;
constexpr int kArtH = 691;
constexpr int kStripH = 70;
constexpr int kWindowW = kArtW;
constexpr int kWindowH = kArtH + kStripH;

// How far the editor may be scaled down before the lettering stops being legible. Same floor as
// rations-amp's, which was measured there against the same two fonts.
constexpr double kMinScale = 0.66;
constexpr double kMaxScale = 2.0;

//--------------------------------------------------------------------------------------------
// The printable face.
constexpr int kFaceLeft = 52;
constexpr int kFaceRight = 414;
constexpr int kFaceTop = 17;
constexpr int kFaceBottom = 674;
constexpr int kFaceW = kFaceRight - kFaceLeft; // 362
constexpr int kFaceCX = (kFaceLeft + kFaceRight) / 2;

//--------------------------------------------------------------------------------------------
// SILKSCREEN — PLAIN WHITE, and nothing behind it.
//
// The head-panel greys are chosen against a dark faceplate and are unreadable on a saturated
// enclosure. So the pedals letter themselves in their own ink, and that ink is white.
//
// WHAT THE ART MEASURES, recorded because it disagrees with the guideline and the disagreement
// should stay visible. WCAG contrast of white against each enclosure's own mean face pixels:
//   Boost   green  3.09     Chorus  yellow 1.86     Flanger red  4.57
//   Delay   blue   4.07     Reverb  lime   1.96
// Three of the five are under the 4.5 : 1 the guideline asks of text this size and two are far
// under it. That is a property of bright enclosures, not of the choice: nothing readable sits well
// on a mid-luminance yellow. panelrender prints all five on every run, so a re-export that makes
// one worse is visible in the build output rather than silent.
//
// TWO WAYS OF PROPPING THE WHITE UP WERE BUILT, RENDERED AND REJECTED in rations-amp, and they are
// recorded here so nobody builds them again. An offset drop shadow GREYS SMALL TEXT OUT: at label
// size Michroma's stems are about one pixel of ink, so nearly every pixel of a legend is an
// antialiased blend and the offset copy lands underneath it rather than beside it. A true outward
// edge — stroking the glyph path, then filling it — fixed the greying and not the look: 0.8 of a
// unit of dark around a one-unit stem reads as a black outline, which is what it is. Black
// lettering with a white outline is worse still, and goes muddy on the Flanger's red.
constexpr uint32_t kPedalInk = 0xFFFFFF;
// The lettering ON A FILLED PLATE, which is the one place this face is not white on colour: a mini
// control that is LIVE inverts — white plate, dark text — and so does a knob's readout while it is
// being dragged. Outlined is idle, filled is live. Used rather than an accent colour because any
// one accent disappears on one of the five enclosures and shouts on another.
constexpr uint32_t kPedalInkPlate = 0x101214;

//--------------------------------------------------------------------------------------------
// THE VERTICAL BUDGET, top to bottom. Four bands share 658 units of face, and every gap between
// them is asserted at the bottom of this file rather than eyeballed.
//
//   grid      one or two rows of controls      knobs first, then any mini controls
//   lamp row  the bat toggle, and the lamp     bypass on the left, the lamp on the centre line
//   switch    the footswitch
//   name      the pedal's own name

// --- the control grid ---
//
// PORTED FROM THE PEDALBOARD, NOT RE-DERIVED. rations-amp draws these same five faces at
// kPedalW = 190 units across (src/geometry.h, kPedalKnobPos and the constants around it); this is
// that layout at the art's own 468, which is 468 / 190 = 2.4632 times larger. Every number below
// is the amp's own multiplied by that and rounded, and each one records what it came from — so a
// pedal opened on its own is the pedal the user already knows from the board, only bigger.
//
//   1-3 knobs  two across the top, a third centred below them. A triangle, which is the Tube
//              Screamer's own layout and the layout of most three-knob pedals.
//   4   knobs  two by two.
//   minis      two fixed slots either side of the lamp — see kMiniCX.
//
// A THREE-ACROSS ROW WAS BUILT HERE AND REMOVED. It looks tidier on paper and it costs the face
// its identity: three columns narrow the legend slot until "Repeats" has to be lettered a third
// smaller than the amp letters it, and every clearance below the knobs then has to move to suit a
// grid nothing else in the family uses. The amp's layout has none of those problems because the
// legends sit BESIDE the lower dial on a three-knob face rather than above it.
constexpr int kKnobR = 49;        // amp kPedalKnobR 20
constexpr int kKnob3DX = 111;     // amp kPedalKnobDX 45
constexpr int kKnob3Row1CY = 108; // amp kPedalKnobRow1Y 44
constexpr int kKnob3MidCY = 190;  // amp kPedalKnobMidY 77
constexpr int kKnob4DX = 91;      // amp kPedalKnob4DX 37
constexpr int kKnob4Row1CY = 99;  // amp kPedalKnob4Row1Y 40
constexpr int kKnob4Row2CY = 246; // amp kPedalKnob4Row2Y 100

constexpr int kKnobLabelSize = 27;  // amp kPedalLabelSize 11 — Michroma, under a control
constexpr int kKnobLabelDY = 30;    // amp kPedalLabelDY 12 — baseline below the control's edge
constexpr int kKnobLabelMargin = 7; // amp kPedalLabelMargin 3 — bare face beside a legend

constexpr int kMaxKnobs = 4;

struct Point {
    int x, y;
};

// Where knob k of n sits. The amp's pedalKnobPos, scaled — including its rule that an ODD count
// puts its last knob on the centre line rather than leaving a hole.
constexpr Point knobPos(int nKnobs, int k)
{
    if (nKnobs <= 1)
        return {kFaceCX, kKnob3Row1CY};
    if (nKnobs <= 3) {
        if (k == 2)
            return {kFaceCX, kKnob3MidCY};
        return {kFaceCX + (k == 0 ? -kKnob3DX : kKnob3DX), kKnob3Row1CY};
    }
    return {kFaceCX + ((k % 2 == 0) ? -kKnob4DX : kKnob4DX), (k < 2) ? kKnob4Row1CY : kKnob4Row2CY};
}

// How wide a legend may render before it reaches the border trim. It is centred on its knob, so
// the binding edge is whichever side is nearer. tools/panelrender measures every legend on every
// face against exactly this, in the real font — the amp learned that the hard way, with a
// "Feedback" that passed a check against the pitch and then drew past the enclosure's edge.
constexpr int knobLabelAllowance(int nKnobs, int k)
{
    const int cx = knobPos(nKnobs, k).x;
    const int l = cx - kFaceLeft;
    const int r = kFaceRight - cx;
    return 2 * (l < r ? l : r) - 2 * kKnobLabelMargin;
}

// --- the lamp ---
constexpr int kLedCX = kFaceCX;
constexpr int kLedCY = 365; // amp kPedalLedY 148
constexpr int kLedR = 17;   // amp kPedalLedR 7

// --- the mini controls ---
//
// A text plate rather than a dial, because what goes here is a list (the Delay's Sync) and a
// two-state switch (its Ping-Pong) and neither reads as a rotation. They sit ON the lamp's row,
// which is the only band with full width and nothing else in it, and they show their VALUE — the
// amp letters no legend under them and there is no room for one here either: the plate ends at
// 385 and the footswitch begins at 409.
constexpr int kMiniCount = 2;
constexpr int kMiniW = 143;                     // amp kPedalMiniW 58
constexpr int kMiniH = 39;                      // amp kPedalMiniH 16
constexpr int kMiniTextSize = 25;               // amp kPedalMiniSize 10
constexpr int kMiniRadius = 7;                  // amp kPedalMiniRadius 3
constexpr int kMiniCX[kMiniCount] = {135, 333}; // amp kPedalMiniCX 55, 135
constexpr int kMiniCY = kLedCY;

// --- footswitch ---
constexpr int kSwitchCX = kFaceCX;
constexpr int kSwitchCY = 463; // amp kPedalSwitchY 188
constexpr int kSwitchR = 54;   // amp kPedalSwitchR 22
// The whole chrome cap is the target, and a little more: this is the control the plug-in is named
// after and it should be impossible to miss.
constexpr int kSwitchHitR = kSwitchR + 10;

// --- the bypass toggle: the one control the pedalboard does not have ---
//
// Inside the amp a pedal has no host bypass of its own — the amp carries one for the whole
// plug-in. Five separate plug-ins each need theirs, so this is the only addition to the ported
// face, and it is put where it costs the layout nothing.
//
// ON THE FOOTSWITCH'S OWN ROW, TO ITS LEFT. That band is empty on all five faces, where the lamp
// row above it is spoken for by the Delay's two mini plates; and standing the host's bypass
// beside the pedal's own switch is what makes them read as the pair they are rather than as one
// control drawn twice. Its legend is lettered smaller than the knobs' — "BYPASS" is a long word
// in a narrow space, and at the knob size it would reach the footswitch's hit box.
//
// 42 x 68 keeps the switch art's own 112:184 aspect to within half a unit.
constexpr int kToggleW = 42;
constexpr int kToggleH = 68;
constexpr int kToggleCX = 112;
constexpr int kToggleCY = kSwitchCY;
constexpr int kToggleLabelSize = 16;
constexpr int kToggleLabelW = 100; // "BYPASS", Michroma at kToggleLabelSize — measured 99.5
constexpr int kToggleLabelDY = 22; // baseline below the toggle's lower edge
// A generous hit box: a bat switch is small and the thing being hit is a mouse pointer, not a
// boot. It reaches down past the legend, because a legend under a switch reads as part of it.
constexpr int kToggleHitW = 104;
constexpr int kToggleHitX = kToggleCX - kToggleHitW / 2;
constexpr int kToggleHitTop = kToggleCY - kToggleH / 2 - 8;
constexpr int kToggleHitBottom = kToggleCY + kToggleH / 2 + kToggleLabelDY + 8;

// --- the pedal's own name ---
constexpr int kNameSize = 49;       // amp kPedalNameSize 20
constexpr int kNameBaselineY = 616; // amp kPedalNameY 250
constexpr int kNameAllowance = kFaceRight - kFaceLeft - 2 * kKnobLabelMargin;

//--------------------------------------------------------------------------------------------
// THE STRIP, below the enclosure. One row: what the footswitch is learned to, and the two buttons
// that change it. Drawn on the window's own background rather than on the art, because it is not
// part of the pedal — no stompbox has a MIDI panel silkscreened on its face.
constexpr uint32_t kStripBg = 0x121011;
constexpr uint32_t kStripRule = 0x2E2A28; // the hairline that separates strip from enclosure

constexpr int kStripInset = 16;
constexpr int kStripRowY = kArtH + kStripInset;
constexpr int kStripRowH = kStripH - 2 * kStripInset;
constexpr int kStripTextSize = 22;
constexpr int kStripLabelSize = 20;

// "MIDI", not "Footswitch". The strip has exactly one row and the footswitch is the only thing
// on the pedal it could be about, so the long word bought nothing and cost the binding text its
// room: at 468 units wide, "Footswitch" plus the two buttons left 64 units for "not learned",
// which is a clip. The static_assert on kStripValueW below is what caught that.
constexpr int kStripLabelX = 24;
constexpr int kStripLabelW = 62; // "MIDI", Michroma at kStripLabelSize
constexpr int kStripValueX = kStripLabelX + kStripLabelW + 12;

constexpr int kStripButtonH = kStripRowH;
constexpr int kStripLearnW = 100;
constexpr int kStripClearW = 84;
constexpr int kStripButtonGap = 10;
constexpr int kStripRightInset = 24;
constexpr int kStripLearnX = kWindowW - kStripRightInset - kStripLearnW;
constexpr int kStripClearX = kStripLearnX - kStripButtonGap - kStripClearW;
constexpr const char *kStripLearnLabel = "Learn";
constexpr const char *kStripListenLabel = "Listening";
constexpr const char *kStripClearLabel = "Clear";
// Where the binding text may run to before it is clipped: up to the Clear button, which is the
// leftmost thing on the right-hand side even when it is not drawn.
constexpr int kStripValueW = kStripClearX - kStripButtonGap - kStripValueX;

//--------------------------------------------------------------------------------------------
// The dial's sweep, shared with the art pipeline: 270 degrees, symmetric about straight up.
constexpr double kKnobSweepDeg = 270.0;

//--------------------------------------------------------------------------------------------
// WHAT THE LAYOUT PROMISES. Everything below is a gap that must not close. A knob added to a
// pedal, a bigger legend or a re-exported enclosure breaks the build here instead of producing a
// face with two things drawn on top of each other. What these CANNOT check is how wide a legend
// actually renders; tools/panelrender does that, against the real font.

// Horizontal: no control may hang over the printable face, on either face shape.
static_assert(knobPos(3, 0).x - kKnobR > kFaceLeft && knobPos(3, 1).x + kKnobR < kFaceRight,
              "a three-knob face's upper pair hangs over the printable face");
static_assert(knobPos(4, 0).x - kKnobR > kFaceLeft && knobPos(4, 1).x + kKnobR < kFaceRight,
              "a four-knob face's pairs hang over the printable face");
static_assert(knobLabelAllowance(3, 0) > 0 && knobLabelAllowance(4, 0) > 0,
              "a legend has no room to render in");
static_assert(kMiniCX[0] - kMiniW / 2 >= kFaceLeft,
              "the left mini plate runs on to the enclosure's border trim");
static_assert(kMiniCX[1] + kMiniW / 2 <= kFaceRight,
              "the right mini plate runs on to the enclosure's border trim");
static_assert(kMiniCX[0] + kMiniW / 2 < kLedCX - kLedR && kMiniCX[1] - kMiniW / 2 > kLedCX + kLedR,
              "a mini plate covers the lamp");

// A three-knob face's upper legends sit BESIDE the centred lower dial, not above it, so the
// clearance that matters there is horizontal. It is deliberately NOT asserted here: the allowance
// above is the distance to the face's edge, and a legend rendered to the full width of it would
// just touch the lower dial — so a compile-time check on the allowance would either fail on a
// layout that is correct or have to be written against a number no legend actually reaches. What
// matters is how wide each legend REALLY renders, which only the font can answer, so
// tools/panelrender measures exactly this clearance on every build.

// Vertical: the bands, in order, none of them touching. A legend's descender is taken as a third
// of its size, which is what the amp assumes and what panelrender confirms against the real font.
constexpr int knobLabelBottom(int cy)
{
    return cy + kKnobR + kKnobLabelDY + kKnobLabelSize / 3;
}
static_assert(knobLabelBottom(kKnob4Row1CY) < kKnob4Row2CY - kKnobR,
              "a four-knob face's upper legends collide with its lower row");
static_assert(knobLabelBottom(kKnob4Row2CY) < kMiniCY - kMiniH / 2 &&
                  knobLabelBottom(kKnob4Row2CY) < kLedCY - kLedR,
              "the lower row's legends collide with the lamp row");
static_assert(knobLabelBottom(kKnob3MidCY) < kLedCY - kLedR,
              "a three-knob face's lower legend collides with the lamp");
static_assert(kMiniCY + kMiniH / 2 < kSwitchCY - kSwitchR,
              "the mini plates collide with the footswitch");
static_assert(kLedCY + kLedR < kSwitchCY - kSwitchR, "the lamp collides with the footswitch");
static_assert(kSwitchCY + kSwitchR < kNameBaselineY - kNameSize,
              "the footswitch collides with the pedal's name");
static_assert(kNameBaselineY + kNameSize / 4 < kFaceBottom,
              "the pedal's name is drawn off the bottom of the face");
static_assert(kKnob4Row1CY - kKnobR > kFaceTop && kKnob3Row1CY - kKnobR > kFaceTop,
              "the top row of dials is drawn off the top of the face");

// The bypass toggle shares the footswitch's row and must not touch it, nor hang off the face.
// The legend is the wide part and is what these are really about — see the note on kToggleLabelW.
static_assert(kToggleCX + kToggleLabelW / 2 < kSwitchCX - kSwitchHitR,
              "the bypass legend reaches the footswitch's hit box");
static_assert(kToggleCX - kToggleLabelW / 2 > kFaceLeft,
              "the bypass legend hangs over the printable face");
static_assert(kToggleHitX + kToggleHitW < kSwitchCX - kSwitchHitR,
              "the bypass toggle's hit box overlaps the footswitch's");
static_assert(kToggleCY - kToggleH / 2 > kMiniCY + kMiniH / 2,
              "the bypass toggle reaches up into the lamp row");
static_assert(kToggleCY + kToggleH / 2 + kToggleLabelDY + kToggleLabelSize / 3 <
                  kNameBaselineY - kNameSize,
              "the bypass legend collides with the pedal's name");

// The strip's own row: the two buttons and the value text must not overlap.
static_assert(kStripValueW > 120, "the binding text has no room left beside the buttons");
static_assert(kStripClearX > kStripValueX, "the strip's buttons overrun its label");
static_assert(kStripRowY + kStripRowH <= kWindowH, "the strip's row is taller than the strip");

// Every pedal's table must produce a face this file can actually draw.
static_assert(knobCount(kBoostParams) <= kMaxKnobs && knobCount(kChorusParams) <= kMaxKnobs &&
                  knobCount(kFlangerParams) <= kMaxKnobs && knobCount(kDelayParams) <= kMaxKnobs &&
                  knobCount(kReverbParams) <= kMaxKnobs,
              "a pedal has more knobs than the face has positions");
static_assert(miniCount(kBoostParams) <= kMiniCount && miniCount(kChorusParams) <= kMiniCount &&
                  miniCount(kFlangerParams) <= kMiniCount &&
                  miniCount(kDelayParams) <= kMiniCount && miniCount(kReverbParams) <= kMiniCount,
              "a pedal has more mini controls than the lamp row has slots");

} // namespace geo
} // namespace Rations
