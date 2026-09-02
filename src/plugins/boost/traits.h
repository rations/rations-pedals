// Rations Boost — everything that distinguishes this plug-in from the other four.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// PedalProcessor, PedalController and PedalView are all templates over a traits struct, and this
// is the whole of what a pedal supplies to them. If something is not here, it is the same in
// every pedal — which is the point: five plug-ins that differ by one struct cannot drift apart in
// their state format, their MIDI learn, their bus negotiation or their editor.
#pragma once

#include "common/pedalids.h"
#include "ids.h"
#include "pedals/boost.h"

namespace Rations
{

struct BoostTraits {
    using Dsp = pedals::Boost;

    static constexpr const char *kName = "Rations Boost";
    static constexpr const char *kShortName = "BOOST"; // drawn on the enclosure
    static constexpr const char *kArt = "pedal-boost"; // resources/img/<art>.png
    static constexpr const char *kSubCategory = "Fx|Distortion";

    static const Steinberg::TUID &processorUID()
    {
        return boost::kProcessorUID;
    }
    static const Steinberg::TUID &controllerUID()
    {
        return boost::kControllerUID;
    }

    static constexpr ParamList kParams = kBoostParams;

    // The 4x half-band oversampler around the clipping stage delays the signal, and a host that is
    // not told compensates for nothing. The number itself is the oversampler's.
    static constexpr bool kReportsLatency = true;

    // Only the Delay reads ProcessContext, and reading it costs a branch on flags every block.
    static constexpr bool kWantsTempo = false;

    // A Tube Screamer is ONE circuit. This plug-in refuses a stereo input arrangement rather than
    // quietly instantiating two of it: the CPU cost of the Newton solve inside the oversampler is
    // the highest of the five, a boost belongs on a mono guitar track, and a host that is told
    // "mono in" offers it exactly where it belongs. A stereo OUTPUT is still accepted, and gets
    // the one signal on both sides — which is what a mono pedal into a stereo rig sounds like.
    static constexpr bool kMonoOnly = true;
};

} // namespace Rations
