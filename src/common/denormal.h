// Flush-to-zero / denormals-are-zero. Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// Re-armed on every process() call rather than once at setActive: a host is not required to set
// FTZ/DAZ on its audio threads, it may run process() on a different thread than the one that
// activated the plug-in, and subnormals in a feedback comb or a smoother stall the CPU badly
// enough to blow the real-time deadline. The call is two register writes.
#pragma once

#if defined(__SSE__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define RATIONS_HAVE_SSE_DENORMAL 1
#endif

namespace Rations
{

inline void setDenormalMode()
{
#ifdef RATIONS_HAVE_SSE_DENORMAL
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

} // namespace Rations
