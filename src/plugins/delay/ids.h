// Rations Delay — this plug-in's identity.
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
namespace delay
{

static DECLARE_UID(kProcessorUID, 0x9500F443, 0x70754DAB, 0xAEBE2A4D, 0x9B937EAC);
static DECLARE_UID(kControllerUID, 0xC987D346, 0x932D4569, 0xADA6DDCD, 0xD1CFA034);

} // namespace delay
} // namespace Rations
