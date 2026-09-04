// Rations Pedals — a non-finite test that survives -ffast-math.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// WHY THIS IS NOT std::isfinite. Every plug-in target and tools/pedalcheck are compiled with
// -ffast-math, which implies -ffinite-math-only, which licenses the compiler to assume that no
// NaN and no infinity ever occurs. Under that assumption std::isfinite(x) folds to the constant
// true and std::isnan(x) to false, so a guard written with either is deleted from the build; and
// so is a guard written as a floating-point comparison, because the compiler is equally free to
// rewrite !(v >= 0.0) as v < 0.0. Measured with this machine's g++ at -O3: given a quiet NaN,
// std::isfinite returns 1 and !(v >= 0.0) returns 0 with -ffast-math on, and 0 and 1 respectively
// with it off.
//
// -ffast-math is not removable here. The golden DSP hashes were recorded from a build that has
// it and the shipped plug-ins are compiled with it, so dropping it would change what the five
// pedals sound like — the one thing that must not move.
//
// So the test has to stay off the floating-point unit and read the bits. IEEE-754 binary64 says
// a value is an infinity or a NaN exactly when its eleven exponent bits are all ones, whatever
// the sign and the significand are, which is one mask and one integer compare. memcpy rather
// than a union or a pointer cast because it is the only spelling that is not a strict-aliasing
// violation, and every compiler this project uses turns it into a register move.
#pragma once

#include <cstdint>
#include <cstring>

namespace Rations
{
namespace dsp
{

// True for an ordinary number or a denormal; false for +/-inf and for every NaN.
inline bool isFinite(double v)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & UINT64_C(0x7FF0000000000000)) != UINT64_C(0x7FF0000000000000);
}

} // namespace dsp
} // namespace Rations
