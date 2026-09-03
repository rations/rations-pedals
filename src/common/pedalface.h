// PedalFace — one pedal, painted onto a Canvas.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// SEPARATE FROM THE VIEW ON PURPOSE. Everything here is Canvas, ImageCache and arithmetic: no
// VST3, no X11, no Win32, no host. That is what lets tools/panelrender link it, render all five
// faces to PNG and run under Wine — which is how the Linux and Windows builds are proved to draw
// the same pixels, and it is the first thing that builds and runs when a Windows toolchain is
// being brought up. A face painter that lived inside the editor could not be checked that way.
#pragma once

#include "pedalgeometry.h"
#include "pedalids.h"

#include "gfx/canvas.h"
#include "gfx/image.h"

namespace Rations
{

// Everything the face draws from. A plain struct of borrowed pointers: the caller owns all of it,
// and the painter neither keeps nor changes any of it.
struct FaceState {
    ParamList params = kBoostParams;
    const double *norm = nullptr; // params.count normalized values, in table order
    const char *name = "";        // drawn on the enclosure, Michroma
    const char *art = "";         // ImageCache key, e.g. "pedal-boost"

    bool bypassed = false; // the bat toggle's state (kBypassId)
    int draggingKnob = -1; // index into the pedal's knobs, or -1; shows that knob's readout

    // The strip under the enclosure.
    const char *bindingText = "not learned";
    bool learned = false; // draws the Clear button
    bool armed = false;   // the Learn button reads "Listening"
};

// The pedal's value as the face prints it: the plain number at the spec's precision, its unit if
// it has one, and the list entry's own name for a List control.
//
// Deliberately NOT the controller's Parameter::toString. The face has to render identically in
// the plug-in and in panelrender, and panelrender has no controller — so the formatting lives
// where both can reach it, and the plug-in uses this one too rather than having two answers.
std::string formatValue(const PedalParamSpec &spec, double norm);

// The enclosure and everything on it. `scale` is the view's current scale factor and is used only
// to choose the pixel size of a cached bitmap; all coordinates are logical.
void drawPedalFace(Canvas &c, ImageCache &images, const FaceState &s, double scale);

// The MIDI strip beneath it. Split out because the enclosure is static between parameter changes
// and the strip is not.
void drawStrip(Canvas &c, const FaceState &s, double scale);

// The geometry of the two strip buttons, in one place, so the painter and the hit test cannot
// drift apart — which for a Clear button that only exists on a learned row is not a theoretical
// risk.
Rect stripLearnRect();
Rect stripClearRect();

} // namespace Rations
