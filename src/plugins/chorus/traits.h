// Rations Chorus — everything that distinguishes this plug-in from the other four.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// See src/plugins/boost/traits.h for what each field is for; this is the same struct with this
// pedal's answers.
#pragma once

#include "common/pedalids.h"
#include "ids.h"
#include "pedals/chorus.h"

namespace Rations
{

struct ChorusTraits {
    using Dsp = pedals::Chorus;

    static constexpr const char *kName = "Rations Chorus";
    static constexpr const char *kShortName = "CHORUS";
    static constexpr const char *kArt = "pedal-chorus";
    static constexpr const char *kSubCategory = "Fx|Modulation";

    static const Steinberg::TUID &processorUID()
    {
        return chorus::kProcessorUID;
    }
    static const Steinberg::TUID &controllerUID()
    {
        return chorus::kControllerUID;
    }

    static constexpr ParamList kParams = kChorusParams;

    static constexpr bool kReportsLatency = false;
    static constexpr bool kWantsTempo = false;
    static constexpr bool kMonoOnly = false;
};

} // namespace Rations
