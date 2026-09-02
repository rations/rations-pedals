// Rations Delay — everything that distinguishes this plug-in from the other four.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// See src/plugins/boost/traits.h for what each field is for; this is the same struct with this
// pedal's answers.
#pragma once

#include "common/pedalids.h"
#include "ids.h"
#include "pedals/delay.h"

namespace Rations
{

struct DelayTraits {
    using Dsp = pedals::Delay;

    static constexpr const char *kName = "Rations Delay";
    static constexpr const char *kShortName = "DELAY";
    static constexpr const char *kArt = "pedal-delay";
    static constexpr const char *kSubCategory = "Fx|Delay";

    static const Steinberg::TUID &processorUID()
    {
        return delay::kProcessorUID;
    }
    static const Steinberg::TUID &controllerUID()
    {
        return delay::kControllerUID;
    }

    static constexpr ParamList kParams = kDelayParams;

    static constexpr bool kReportsLatency = false;
    // The one pedal that reads ProcessContext: its Sync divisions are beats, and a beat is only
    // a length of time once a tempo says so. Gated on kTempoValid at the point of use — a host is
    // entitled to supply no context at all.
    static constexpr bool kWantsTempo = true;
    static constexpr bool kMonoOnly = false;
};

} // namespace Rations
