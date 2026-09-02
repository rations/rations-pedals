// Rations Reverb — this plug-in's identity.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// THE TWO UIDS ARE THE PLUG-IN. A host remembers a plug-in by its processor UID and reopens a
// saved project by looking that number up, so these are permanent from the moment anything is
// released: changing one does not rename a plug-in, it replaces it with a different one and
// silently drops it out of every project that used it.
//
// Generated with uuidgen on 2026-09-02, one pair per pedal, ten in all. NOT derived from
// rations-amp's two and NOT derived from each other — a scheme that computed them from a base
// would put five plug-ins one typo apart.
#pragma once

#include "pluginterfaces/base/funknown.h"

namespace Rations
{
namespace reverb
{

static DECLARE_UID(kProcessorUID, 0x198A9FCE, 0xC3334BDD, 0xAEA993DB, 0x8AE3B01B);
static DECLARE_UID(kControllerUID, 0x89E09086, 0xE92D43F8, 0xA3531EF0, 0xE82A9CD5);

} // namespace reverb
} // namespace Rations
