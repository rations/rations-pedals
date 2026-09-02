// Rations Boost — the plug-in factory. Two classes: the audio component and its controller.
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
namespace boost
{
using Processor = PedalProcessor<BoostTraits>;
using Controller = PedalController<BoostTraits>;
} // namespace boost
} // namespace Rations

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(Rations::boost::kProcessorUID, Steinberg::PClassInfo::kManyInstances,
          kVstAudioEffectClass, stringPluginName, Steinberg::Vst::kDistributable, "Fx|Distortion",
          FULL_VERSION_STR, kVstVersionString, Rations::boost::Processor::createInstance, nullptr)

DEF_CLASS(Rations::boost::kControllerUID, Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass, stringPluginName "Controller", 0, "", FULL_VERSION_STR,
          kVstVersionString, Rations::boost::Controller::createInstance, nullptr)

END_FACTORY
