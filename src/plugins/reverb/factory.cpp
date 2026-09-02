// Rations Reverb — the plug-in factory. Two classes: the audio component and its controller.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// kDistributable says the two halves may live in different processes, which is true here: nothing
// the controller holds is a pointer into the processor, and the only thing that crosses between
// them is the MIDI learn message and the state blob.

#include "common/pedalcontroller.h"
#include "common/pedalprocessor.h"
#include "common/version.h"
#include "ids.h"
#include "traits.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

namespace Rations
{
namespace reverb
{
using Processor = PedalProcessor<ReverbTraits>;
using Controller = PedalController<ReverbTraits>;
} // namespace reverb
} // namespace Rations

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(Rations::reverb::kProcessorUID, Steinberg::PClassInfo::kManyInstances,
          kVstAudioEffectClass, stringPluginName, Steinberg::Vst::kDistributable, "Fx|Reverb",
          FULL_VERSION_STR, kVstVersionString, Rations::reverb::Processor::createInstance, nullptr)

DEF_CLASS(Rations::reverb::kControllerUID, Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass, stringPluginName "Controller", 0, "", FULL_VERSION_STR,
          kVstVersionString, Rations::reverb::Controller::createInstance, nullptr)

END_FACTORY
