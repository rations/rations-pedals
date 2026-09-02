// PedalFace implementation. See pedalface.h for why this is not part of the view.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).

#include "pedalface.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace Rations
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

// The dial art is rendered at a multiple of its drawn size and downscaled by Cairo. A rotated
// bitmap sampled at exactly its final size shows stair-stepping on the pointer, which is the one
// part of a knob the eye tracks.
constexpr int kDialSupersample = 2;

void drawInk(Canvas &c, const char *text, float x, float y)
{
    c.setColor(geo::kPedalInk);
    c.drawString(text, x, y);
}

// A rounded plate with text in it. Idle is outlined; LIVE is filled, and inverts its text. That
// pairing is used rather than an accent colour because any one accent disappears on one of the
// five enclosures and shouts on another.
void drawPlate(Canvas &c, const Rect &r, const char *text, float textSize, bool live)
{
    if (live) {
        c.setColor(geo::kPedalInk, 235);
        c.fillRoundRect(r, 6.0f);
    } else {
        c.setColor(0x000000, 90);
        c.fillRoundRect(r, 6.0f);
        c.setColor(geo::kPedalInk, 200);
        c.setPenSize(1.5f);
        c.strokeRoundRect(r, 6.0f);
    }
    c.setFont(Font::Body);
    c.setFontSize(textSize);
    c.setColor(live ? geo::kPedalInkPlate : geo::kPedalInk);
    const std::string fit = c.clipToWidth(text, r.w - 10.0f);
    c.drawString(fit.c_str(), r.centerX() - c.stringWidth(fit.c_str()) * 0.5f,
                 r.centerY() + textSize * 0.36f);
}

void drawKnobAt(Canvas &c, ImageCache &images, float cx, float cy, float r, double norm,
                double scale)
{
    const Rect face(cx - r, cy - r, 2.0f * r, 2.0f * r);
    const int px = static_cast<int>(std::lround(2.0 * r * scale * kDialSupersample));
    if (cairo_surface_t *dial = images.getScaled("dial", px, px)) {
        c.drawImageRotated(dial, face, (norm - 0.5) * geo::kKnobSweepDeg);
        return;
    }
    // Degradation path for a missing dial PNG: a disc and a pointer, tracking the art's own
    // sweep so a missing asset does not also move the indication.
    c.setColor(0x28282E);
    c.fillEllipse(face);
    c.setColor(geo::kGold);
    c.setPenSize(2.0f);
    const double a = (norm - 0.5) * geo::kKnobSweepDeg * kPi / 180.0;
    c.strokeLine(cx, cy, cx + static_cast<float>(std::sin(a)) * r * 0.8f,
                 cy - static_cast<float>(std::cos(a)) * r * 0.8f);
    c.setPenSize(1.0f);
}

// `on` is the parameter, not the bat. Bypass ENGAGED points the bat DOWN, which is how an amp
// reads: up is the normal state.
void drawBatToggle(Canvas &c, ImageCache &images, bool bypassed, double scale)
{
    const bool batUp = !bypassed;
    const Rect dest(static_cast<float>(geo::kToggleCX - geo::kToggleW / 2),
                    static_cast<float>(geo::kToggleCY - geo::kToggleH / 2),
                    static_cast<float>(geo::kToggleW), static_cast<float>(geo::kToggleH));
    const int pw = static_cast<int>(std::lround(geo::kToggleW * scale));
    const int ph = static_cast<int>(std::lround(geo::kToggleH * scale));
    if (cairo_surface_t *s =
            images.getScaled(batUp ? "switch_up_ring" : "switch_down_ring", pw, ph)) {
        c.drawImage(s, dest);
    } else {
        // The lever is fixed at the CENTRE and tapers to a ball at the travelling end, so what
        // moves is the ball. A crude shape is fine; one that contradicts the art is not, because
        // it inverts which end the eye reads as fixed.
        const float cx = dest.centerX(), cy = dest.centerY();
        const float ballR = dest.w * 0.26f;
        const float ballY = batUp ? dest.y + ballR + 2.0f : dest.bottom() - ballR - 2.0f;
        c.setColor(batUp ? 0xE8EAEC : 0x9A9EA2);
        c.fillRect(Rect(cx - 2.0f, std::min(cy, ballY), 4.0f, std::fabs(ballY - cy)));
        c.fillEllipse(cx, ballY, ballR, ballR);
    }

    // The legend NAMES the switch, it does not report it. Dimming it when bypass is off would
    // read as "disabled", and bypass off is the normal state. The bat carries the state.
    //
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kToggleLabelSize));
    const float w = c.stringWidth("BYPASS");
    drawInk(c, "BYPASS", geo::kToggleCX - w * 0.5f,
            static_cast<float>(geo::kToggleCY + geo::kToggleH / 2 + geo::kToggleLabelDY));
}

void drawLamp(Canvas &c, ImageCache &images, bool lit, double scale)
{
    const float r = static_cast<float>(geo::kLedR);
    const float cx = static_cast<float>(geo::kLedCX), cy = static_cast<float>(geo::kLedCY);
    const Rect dest(cx - r, cy - r, 2.0f * r, 2.0f * r);
    const int px = static_cast<int>(std::lround(2.0 * r * scale));
    if (cairo_surface_t *led = images.getScaled(lit ? "led_on" : "led_off", px, px)) {
        c.drawImage(led, dest);
        return;
    }
    c.setColor(lit ? 0xFF4438 : 0x3A2B29);
    c.fillEllipse(cx, cy, r, r);
}

void drawFootswitch(Canvas &c, ImageCache &images, double scale)
{
    const float r = static_cast<float>(geo::kSwitchR);
    const float cx = static_cast<float>(geo::kSwitchCX), cy = static_cast<float>(geo::kSwitchCY);
    const Rect dest(cx - r, cy - r, 2.0f * r, 2.0f * r);
    const int px = static_cast<int>(std::lround(2.0 * r * scale));
    if (cairo_surface_t *cap = images.getScaled("pedal_switch", px, px)) {
        c.drawImage(cap, dest);
        return;
    }
    c.setColor(0xB9BCC0);
    c.fillEllipse(cx, cy, r, r);
    c.setColor(0x6E7378);
    c.setPenSize(2.0f);
    c.strokeEllipse(cx, cy, r * 0.72f, r * 0.72f);
    c.setPenSize(1.0f);
}

// A strip button. Not on the enclosure, so it uses the window's own colours rather than the
// pedal's ink.
void drawStripButton(Canvas &c, const Rect &r, const char *label, bool active)
{
    c.setColor(active ? geo::kAccent : 0x000000, active ? 210 : 120);
    c.fillRoundRect(r, 5.0f);
    c.setColor(active ? geo::kAccent : geo::kDimColor, active ? 255 : 170);
    c.setPenSize(1.0f);
    c.strokeRoundRect(r, 5.0f);
    c.setFont(Font::Body);
    c.setFontSize(static_cast<float>(geo::kStripLabelSize));
    c.setColor(active ? 0x101214 : geo::kTextColor);
    const std::string fit = c.clipToWidth(label, r.w - 8.0f);
    c.drawString(fit.c_str(), r.centerX() - c.stringWidth(fit.c_str()) * 0.5f,
                 r.centerY() + geo::kStripLabelSize * 0.36f);
}
} // namespace

//------------------------------------------------------------------------------------------------
std::string formatValue(const PedalParamSpec &spec, double norm)
{
    const double plain = pedalPlain(spec, norm);
    if (spec.kind == PedalParamKind::List) {
        // Only the Delay's Sync division today. Indexed rather than searched, and clamped,
        // because norm is untrusted the moment it arrives from a host.
        const int i = static_cast<int>(plain);
        if (i >= 0 && i < kDelaySyncCount)
            return kDelaySyncNames[i];
        return "-";
    }
    if (spec.kind == PedalParamKind::Toggle)
        return plain > 0.5 ? "On" : "Off";

    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", spec.precision, plain);
    std::string s = buf;
    if (spec.unit) {
        s += " ";
        s += spec.unit;
    }
    return s;
}

//------------------------------------------------------------------------------------------------
void drawPedalFace(Canvas &c, ImageCache &images, const FaceState &s, double scale)
{
    // The enclosure. Blank art at its own native size, so nothing is resampled and every legend
    // below is drawn over a pixel-exact background.
    if (cairo_surface_t *art = images.get(s.art)) {
        c.drawImage(art,
                    Rect(0, 0, static_cast<float>(geo::kArtW), static_cast<float>(geo::kArtH)));
    } else {
        c.setColor(geo::kFaceColor);
        c.fillRect(Rect(0, 0, static_cast<float>(geo::kArtW), static_cast<float>(geo::kArtH)));
    }

    if (!s.norm)
        return;

    // The grid, in table order: every knob, then every mini control. A control's SLOT is decided
    // by its position in that sequence and nothing else, which is what makes adding one a
    // one-line change to the pedal's table.
    const int knobs = knobCount(s.params);
    const int minis = miniCount(s.params);
    const int items = knobs + minis;
    const float knobR = static_cast<float>(geo::gridKnobR(items));

    int item = 0;
    for (int row = 0; row < geo::gridRows(items); ++row) {
        const int n = geo::gridRowItems(items, row);
        const float cy = static_cast<float>(geo::gridCY(items, row));
        for (int col = 0; col < n; ++col, ++item) {
            const int idx =
                item < knobs ? knobParam(s.params, item) : miniParam(s.params, item - knobs);
            if (idx < 0)
                continue;
            const PedalParamSpec &spec = s.params[idx];
            const float cx = static_cast<float>(geo::gridCX(items, row, col));

            if (item < knobs) {
                drawKnobAt(c, images, cx, cy, knobR, s.norm[idx], scale);
            } else {
                // LIVE means "this control is doing something": a sync division other than Free,
                // or ping-pong switched on. An idle control is outlined, so a face does not carry
                // bright plates that mean nothing.
                const std::string val = formatValue(spec, s.norm[idx]);
                const bool live = spec.kind == PedalParamKind::Toggle
                                      ? s.norm[idx] > 0.5
                                      : pedalPlain(spec, s.norm[idx]) > 0.5;
                const Rect plate(cx - geo::kMiniW * 0.5f, cy - geo::kMiniH * 0.5f,
                                 static_cast<float>(geo::kMiniW), static_cast<float>(geo::kMiniH));
                drawPlate(c, plate, val.c_str(), static_cast<float>(geo::kMiniTextSize), live);
            }

            // The legend, always, and at the same height whichever kind of control it names. It
            // never dims: on these enclosures a dimmed legend is an invisible one, and the lamp
            // is what carries state.
            c.setFont(Font::Title);
            c.setFontSize(static_cast<float>(geo::kKnobLabelSize));
            const std::string fit = c.clipToWidth(spec.legend, static_cast<float>(geo::kGridSlotW));
            // One baseline for the whole row, whichever kind of control sits in each slot: a
            // legend under a plate and a legend under a dial must line up or the row looks bent.
            drawInk(c, fit.c_str(), cx - c.stringWidth(fit.c_str()) * 0.5f,
                    cy + knobR + static_cast<float>(geo::kKnobLabelDY));

            // The readout, only while this dial is being dragged, and ABOVE the dial so it never
            // covers the legend that says which dial it is.
            if (item < knobs && item == s.draggingKnob) {
                const std::string val = formatValue(spec, s.norm[idx]);
                c.setFont(Font::Body);
                c.setFontSize(static_cast<float>(geo::kKnobValueSize));
                const float w = c.stringWidth(val.c_str()) + 14.0f;
                const Rect plate(cx - w * 0.5f,
                                 cy - geo::kKnobR -
                                     static_cast<float>(geo::kKnobValueDY + geo::kKnobValueSize),
                                 w, static_cast<float>(geo::kKnobValueSize + 8));
                drawPlate(c, plate, val.c_str(), static_cast<float>(geo::kKnobValueSize), true);
            }
        }
    }

    drawBatToggle(c, images, s.bypassed, scale);

    // The lamp says whether the pedal is IN CIRCUIT, which is the footswitch and the host's
    // bypass together: a pedal switched on inside a bypassed plug-in is not in circuit, and a
    // lamp that claimed otherwise would be the only thing on the face that lied.
    drawLamp(c, images, s.norm[0] > 0.5 && !s.bypassed, scale);
    drawFootswitch(c, images, scale);

    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kNameSize));
    const float nameW = c.stringWidth(s.name);
    drawInk(c, s.name, geo::kFaceCX - nameW * 0.5f, static_cast<float>(geo::kNameBaselineY));
}

//------------------------------------------------------------------------------------------------
Rect stripLearnRect()
{
    return Rect(static_cast<float>(geo::kStripLearnX), static_cast<float>(geo::kStripRowY),
                static_cast<float>(geo::kStripLearnW), static_cast<float>(geo::kStripButtonH));
}
Rect stripClearRect()
{
    return Rect(static_cast<float>(geo::kStripClearX), static_cast<float>(geo::kStripRowY),
                static_cast<float>(geo::kStripClearW), static_cast<float>(geo::kStripButtonH));
}

void drawStrip(Canvas &c, SvgCache &svgs, const FaceState &s, double scale)
{
    (void)svgs;
    (void)scale;
    c.setColor(geo::kStripBg);
    c.fillRect(Rect(0, static_cast<float>(geo::kArtH), static_cast<float>(geo::kWindowW),
                    static_cast<float>(geo::kStripH)));
    c.setColor(geo::kStripRule);
    c.setPenSize(1.0f);
    c.strokeLine(0, static_cast<float>(geo::kArtH) + 0.5f, static_cast<float>(geo::kWindowW),
                 static_cast<float>(geo::kArtH) + 0.5f);

    const float baseline =
        static_cast<float>(geo::kStripRowY) + geo::kStripRowH * 0.5f + geo::kStripTextSize * 0.36f;

    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kStripLabelSize));
    c.setColor(geo::kDimColor);
    c.drawString("MIDI", static_cast<float>(geo::kStripLabelX), baseline);

    // What the footswitch is bound to, or that it is not. Accent while a row is listening,
    // because that is the one state the user is waiting on.
    c.setFont(Font::Body);
    c.setFontSize(static_cast<float>(geo::kStripTextSize));
    c.setColor(s.armed ? geo::kAccent : (s.learned ? geo::kTextColor : geo::kDimColor));
    const std::string fit = c.clipToWidth(s.armed ? "press a footswitch..." : s.bindingText,
                                          static_cast<float>(geo::kStripValueW));
    c.drawString(fit.c_str(), static_cast<float>(geo::kStripValueX), baseline);

    drawStripButton(c, stripLearnRect(), s.armed ? geo::kStripListenLabel : geo::kStripLearnLabel,
                    s.armed);
    // Clear exists only on a learned row: a button that would do nothing is a button that
    // teaches the wrong thing about what the row holds.
    if (s.learned)
        drawStripButton(c, stripClearRect(), geo::kStripClearLabel, false);
}

} // namespace Rations
