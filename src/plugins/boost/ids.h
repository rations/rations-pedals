// Rations Boost — this plug-in's identity.
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
namespace boost
{

static DECLARE_UID(kProcessorUID, 0xDBE143E5, 0x2E7445FE, 0xAF68AAB4, 0x033DDF54);
static DECLARE_UID(kControllerUID, 0x9082A5BC, 0xA4C64907, 0xA6A5A043, 0x76DEA899);

} // namespace boost
} // namespace Rations
