// Rations Reverb — everything that distinguishes this plug-in from the other four.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// See src/plugins/boost/traits.h for what each field is for; this is the same struct with this
// pedal's answers.
#pragma once

#include "common/pedalids.h"
#include "ids.h"
#include "pedals/reverb.h"

namespace Rations
{

struct ReverbTraits {
    using Dsp = pedals::Reverb;

    static constexpr const char *kName = "Rations Reverb";
    static constexpr const char *kShortName = "REVERB";
    static constexpr const char *kArt = "pedal-reverb";
    static constexpr const char *kSubCategory = "Fx|Reverb";

    static const Steinberg::TUID &processorUID()
    {
        return reverb::kProcessorUID;
    }
    static const Steinberg::TUID &controllerUID()
    {
        return reverb::kControllerUID;
    }

    static constexpr ParamList kParams = kReverbParams;

    static constexpr bool kReportsLatency = false;
    static constexpr bool kWantsTempo = false;
    static constexpr bool kMonoOnly = false;
};

} // namespace Rations
