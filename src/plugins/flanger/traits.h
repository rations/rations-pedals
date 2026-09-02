// Rations Flanger — everything that distinguishes this plug-in from the other four.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// See src/plugins/boost/traits.h for what each field is for; this is the same struct with this
// pedal's answers.
#pragma once

#include "common/pedalids.h"
#include "ids.h"
#include "pedals/flanger.h"

namespace Rations
{

struct FlangerTraits {
    using Dsp = pedals::Flanger;

    static constexpr const char *kName = "Rations Flanger";
    static constexpr const char *kShortName = "FLANGER";
    static constexpr const char *kArt = "pedal-flanger";
    static constexpr const char *kSubCategory = "Fx|Modulation";

    static const Steinberg::TUID &processorUID()
    {
        return flanger::kProcessorUID;
    }
    static const Steinberg::TUID &controllerUID()
    {
        return flanger::kControllerUID;
    }

    static constexpr ParamList kParams = kFlangerParams;

    static constexpr bool kReportsLatency = false;
    static constexpr bool kWantsTempo = false;
    static constexpr bool kMonoOnly = false;
};

} // namespace Rations
