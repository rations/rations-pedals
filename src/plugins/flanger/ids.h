// Rations Flanger — this plug-in's identity.
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
namespace flanger
{

static DECLARE_UID(kProcessorUID, 0xBE43220D, 0x5EB94E79, 0x81B00D25, 0x075DDB55);
static DECLARE_UID(kControllerUID, 0xFB106E3E, 0x3CFB4EFE, 0x91858537, 0xC35B436F);

} // namespace flanger
} // namespace Rations
