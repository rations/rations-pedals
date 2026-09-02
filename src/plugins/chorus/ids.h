// Rations Chorus — this plug-in's identity.
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
namespace chorus
{

static DECLARE_UID(kProcessorUID, 0x72EA1756, 0x86B24DEB, 0xBEF3A559, 0xA8B9C90E);
static DECLARE_UID(kControllerUID, 0x0DBD1BB5, 0x32EB426A, 0x93A2C264, 0x2DAB64BE);

} // namespace chorus
} // namespace Rations
