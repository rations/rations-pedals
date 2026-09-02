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
// Knobs and mini controls share ONE grid, at most three across and at most two down, each row
// centred on the face. That is what makes the Delay's Sync and Ping-Pong ordinary members of the
// layout instead of two special cases wedged in beside the lamp, and it is what keeps all five
// faces the same shape below the grid.
//
//   3 items  ->  [ 3 ]          Boost, Chorus
//   4 items  ->  [ 2 ][ 2 ]     Flanger, Reverb
//   6 items  ->  [ 3 ][ 3 ]     Delay — four knobs, then Sync and Ping-Pong
//
// A row of two uses the same PITCH as a row of three rather than spreading to fill the face: the
// legend slot is then one width everywhere, which is the only reason kKnobLabelSize can be a
// single number.
constexpr int kGridCols = 3;
constexpr int kGridRows = 2;
// A one-row face has the whole grid band to itself, so its dials are drawn larger — 104 units
// across in a 118-unit slot, against 84 on a two-row face. Not for its own sake: at 42 a lone row
// of three reads as small controls stranded in a large empty area, and the empty area is not the
// problem (a real three-knob pedal has exactly that space between its dials and its footswitch)
// — the smallness is.
constexpr int kKnobR = 42;
constexpr int kKnobRSingle = 52;
constexpr int kGridPitch = 118;
constexpr int kGridRowCY[kGridRows] = {135, 275};

constexpr int kKnobLabelSize = 20; // Michroma, the legend under a control
constexpr int kKnobLabelDY = 28;   // baseline below the control's lower edge
constexpr int kKnobValueSize = 24; // Roboto, the readout shown while a dial is dragged
constexpr int kKnobValueDY = 28;   // above the dial's UPPER edge, so it never covers the legend

// WHY 20 AND NOT SOMETHING ROUNDER. It is the largest size at which every legend on every face
// fits its slot, and the binding constraint is the Delay's "Repeats": Michroma renders it 149
// units wide at size 27, against a slot of kGridPitch - 6 = 112. One size for all five rather
// than a per-face fit, because two plug-ins from the same set open side by side and a Boost whose
// lettering is a third larger than a Delay's looks like a mistake rather than a fit.
//
// tools/panelrender measures every legend against this on every build; it is not a calculation
// that can be trusted to stay true when a knob is renamed.
constexpr int kGridSlotW = kGridPitch - 6;

// How many rows a face of n controls uses, and how many sit on a given row. Balanced rather than
// filled: four controls are two and two, not three and one.
constexpr int gridRows(int items)
{
    return items <= kGridCols ? 1 : 2;
}
constexpr int gridRowItems(int items, int row)
{
    if (gridRows(items) == 1)
        return items;
    return row == 0 ? (items + 1) / 2 : items - (items + 1) / 2;
}
constexpr int gridCX(int items, int row, int col)
{
    const int n = gridRowItems(items, row);
    return kFaceCX + (2 * col - (n - 1)) * kGridPitch / 2;
}
// A control's radius depends only on how many rows its face uses.
constexpr int gridKnobR(int items)
{
    return gridRows(items) == 1 ? kKnobRSingle : kKnobR;
}
constexpr int gridCY(int items, int row)
{
    // A single row sits between the two row positions, so a three-control face is not
    // top-heavy with an empty band under it.
    return gridRows(items) == 1 ? (kGridRowCY[0] + kGridRowCY[1]) / 2 : kGridRowCY[row];
}

// The mini controls' plates, sized to sit in a grid slot like a dial does.
constexpr int kMiniW = 104;
constexpr int kMiniH = 44;
constexpr int kMiniTextSize = 22;

// --- the lamp row: the bypass toggle and the lamp ---
//
// The bat toggle is the HOST's bypass (kBypassId) and not the footswitch (kSwitchId) — see the
// note on those two in pedalids.h.
//
// IT SITS LEFT OF THE LAMP, not on the centre line, and its legend sits UNDER IT. Putting the
// legend beside the toggle was tried first, to save the 27 units the stacked pair costs, and it
// does not work: Michroma renders "BYPASS" 124 units wide at this size, so a legend starting
// clear of the toggle ends up underneath the lamp. Stacked and pushed to the left of the face,
// both fit with room, and the toggle is then plainly a different control from the footswitch on
// the centre line below it — which is the distinction the whole arrangement has to carry.
//
// 42 x 68 keeps the art's own 112:184 aspect to within half a unit.
constexpr int kToggleW = 42;
constexpr int kToggleH = 68;
constexpr int kToggleCX = 118;
constexpr int kLampRowCY = 400;
constexpr int kToggleCY = kLampRowCY;
constexpr int kToggleLabelSize = 20;
constexpr int kToggleLabelW = 126; // "BYPASS", Michroma at kToggleLabelSize — measured 124.4
constexpr int kToggleLabelDY = 22; // baseline below the toggle's lower edge
// A generous hit box: a bat switch is small and the thing being hit is a mouse pointer, not a
// boot. It reaches down past the legend, because a legend under a switch reads as part of it.
constexpr int kToggleHitW = 140;
constexpr int kToggleHitX = kToggleCX - kToggleHitW / 2;
constexpr int kToggleHitTop = kToggleCY - kToggleH / 2 - 8;
constexpr int kToggleHitBottom = kToggleCY + kToggleH / 2 + kToggleLabelDY + 8;

constexpr int kLedCX = kFaceCX;
constexpr int kLedCY = kLampRowCY;
constexpr int kLedR = 17;

// --- footswitch ---
constexpr int kSwitchCX = kFaceCX;
constexpr int kSwitchCY = 540;
constexpr int kSwitchR = 54;
// The whole chrome cap is the target, and a little more: this is the control the plug-in is named
// after and it should be impossible to miss.
constexpr int kSwitchHitR = kSwitchR + 10;

// --- the pedal's own name ---
constexpr int kNameSize = 46;
constexpr int kNameBaselineY = 650;

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

// Horizontal: no control may hang over the printable face, on any row shape a table can produce.
static_assert(gridCX(3, 0, 0) - kKnobRSingle > kFaceLeft &&
                  gridCX(3, 0, 2) + kKnobRSingle < kFaceRight,
              "a row of three does not fit inside the printable face");
static_assert(gridCX(4, 0, 0) - kMiniW / 2 > kFaceLeft && gridCX(4, 0, 1) + kMiniW / 2 < kFaceRight,
              "a row of two does not fit inside the printable face");
static_assert(gridCX(6, 0, 0) - kMiniW / 2 > kFaceLeft && gridCX(6, 0, 2) + kMiniW / 2 < kFaceRight,
              "a row of three mini plates does not fit inside the printable face");
static_assert(kGridPitch >= 2 * kKnobRSingle && kGridPitch >= kMiniW,
              "adjacent controls on a row overlap");

// The readout is drawn ABOVE a dial while it is dragged, and must stay on the face.
static_assert(kGridRowCY[0] - kKnobR - kKnobValueDY - kKnobValueSize > kFaceTop &&
                  gridCY(3, 0) - kKnobRSingle - kKnobValueDY - kKnobValueSize > kFaceTop,
              "a dragged dial's readout is drawn off the top of the face");

// Vertical: the bands, in order, none of them touching.
constexpr int kGridLabelBottom(int row)
{
    return kGridRowCY[row] + kKnobR + kKnobLabelDY + kKnobLabelSize / 3;
}
static_assert(kGridLabelBottom(0) < kGridRowCY[1] - kKnobR,
              "the upper row's legends collide with the lower row");
static_assert(kGridLabelBottom(1) < kLampRowCY - kToggleH / 2,
              "the lower row's legends collide with the bypass toggle");
static_assert(gridCY(3, 0) + kKnobRSingle + kKnobLabelDY + kKnobLabelSize / 3 <
                  kLampRowCY - kToggleH / 2,
              "a one-row face's legends collide with the bypass toggle");
static_assert(kLampRowCY + kToggleH / 2 + kToggleLabelDY + kToggleLabelSize / 3 <
                  kSwitchCY - kSwitchR,
              "the bypass legend collides with the footswitch");
static_assert(kSwitchCY + kSwitchR < kNameBaselineY - kNameSize,
              "the footswitch collides with the pedal's name");
static_assert(kNameBaselineY + kNameSize / 4 < kFaceBottom,
              "the pedal's name is drawn off the bottom of the face");

// The toggle, its legend and the lamp share one row without meeting. The legend is the wide part
// and is what these are really about — see the note on kToggleLabelW.
static_assert(kToggleCX + kToggleLabelW / 2 < kLedCX - kLedR - 8,
              "the bypass legend runs into the lamp");
static_assert(kToggleCX - kToggleLabelW / 2 > kFaceLeft,
              "the bypass legend hangs over the printable face");
static_assert(kToggleHitX + kToggleHitW < kLedCX - kLedR,
              "the bypass toggle's hit box reaches the lamp");

// The strip's own row: the two buttons and the value text must not overlap.
static_assert(kStripValueW > 120, "the binding text has no room left beside the buttons");
static_assert(kStripClearX > kStripValueX, "the strip's buttons overrun its label");
static_assert(kStripRowY + kStripRowH <= kWindowH, "the strip's row is taller than the strip");

// Every pedal's table must produce a grid this file can actually draw.
static_assert(knobCount(kBoostParams) + miniCount(kBoostParams) <= kGridCols * kGridRows &&
                  knobCount(kChorusParams) + miniCount(kChorusParams) <= kGridCols * kGridRows &&
                  knobCount(kFlangerParams) + miniCount(kFlangerParams) <= kGridCols * kGridRows &&
                  knobCount(kDelayParams) + miniCount(kDelayParams) <= kGridCols * kGridRows &&
                  knobCount(kReverbParams) + miniCount(kReverbParams) <= kGridCols * kGridRows,
              "a pedal has more controls than the grid has slots");

} // namespace geo
} // namespace Rations
