// Flush-to-zero / denormals-are-zero. Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// Re-armed on every process() call rather than once at setActive: a host is not required to set
// FTZ/DAZ on its audio threads, it may run process() on a different thread than the one that
// activated the plug-in, and subnormals in a feedback comb or a smoother stall the CPU badly
// enough to blow the real-time deadline. The call is a register read and at most one write.
//
// x86-64 needs two bits and they are not the same bit:
//   FTZ  flushes subnormal RESULTS to zero
//   DAZ  treats subnormal INPUTS as zero
//
// AArch64 needs one. FPCR bit 24 (FZ) does both jobs at once -- there is no separate DAZ control,
// so arming FZ alone is the complete equivalent of the x86 pair rather than half of it. Measured
// rather than assumed: with FZ clear, a subnormal input multiplied up into normal range keeps its
// value; with FZ set the same multiply yields exactly zero, which can only happen if the input was
// flushed before the multiply.
//
// The reference for the register access is deps/wdl/denormal.h, vendored two directories from
// here, which does the same mrs/msr pair against the same bit 24 mask and likewise skips the write
// when the bit is already set.
//
// This file previously guarded on __SSE__ alone and so compiled to an EMPTY function on every
// other target, which meant the real-time contract above was silently unmet on ARM. The fallback
// arm is still a no-op because there is nothing correct to put there, but it now says so and sets
// RATIONS_DENORMAL_ARMED to 0 so the gap is visible rather than audible.
#pragma once

#if defined(__SSE__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define RATIONS_HAVE_SSE_DENORMAL 1
#define RATIONS_DENORMAL_ARMED 1
#elif defined(__aarch64__)
#define RATIONS_HAVE_FPCR_DENORMAL 1
#define RATIONS_DENORMAL_ARMED 1
#else
#define RATIONS_DENORMAL_ARMED 0
#endif

namespace Rations
{

inline void setDenormalMode()
{
#if defined(RATIONS_HAVE_SSE_DENORMAL)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(RATIONS_HAVE_FPCR_DENORMAL)
    // Read-modify-write: every other bit in FPCR belongs to someone else -- the rounding mode
    // above all -- and a blind write would silently reset it.
    unsigned long fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    const unsigned long armed = fpcr | (1UL << 24);
    if (armed != fpcr) {
        __asm__ __volatile__("msr fpcr, %0" : : "r"(armed));
    }
#endif
}

} // namespace Rations
