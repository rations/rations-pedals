// Rations Pedals — the sample type, and the one interpolator the delay lines need.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// WHY THIS FILE EXISTS. In rations-amp the pedals sat beside a Neural Amp Modeler, so
// AudioDSPTools was already in the include graph and they took two things out of it: the
// DSP_SAMPLE macro and dsp::_CubicInterpolation. Here there is no amp, and pulling
// AudioDSPTools in for those two would drag NeuralAmpModelerCore and Eigen behind it —
// tens of thousands of lines, none of which a stompbox executes.
//
// So both are reproduced here, and the whole point is that they are reproduced EXACTLY.
// The pedal proofs in tools/pedalcheck.cpp assert that every pedal's output is
// bit-identical to what the same DSP produces inside rations-amp, and that claim is only
// worth making if the arithmetic underneath is the same arithmetic. Nothing in this file
// may be "tidied": not the macro's spelling, not the interpolator's factored form.
//
// Sources, both MIT, Copyright (c) 2022, 2023 Steven Atkinson, from AudioDSPTools
// (https://github.com/sdatkinson/AudioDSPTools) — see NOTICE:
//   * DSP_SAMPLE          dsp/dsp.h:10-14
//   * _CubicInterpolation dsp/Resample.h:26-34
#pragma once

// AudioDSPTools spells this as a macro rather than a typedef, and the pedals were written
// against the macro — `std::vector<DSP_SAMPLE>`, `DSP_SAMPLE *l`. Reproduced with its own
// DSP_SAMPLE_FLOAT escape hatch so the two spellings cannot drift, even though nothing in
// this project defines it: a double is what the WDL reverb engine and the Boost's Newton
// solver both want, and a float would change every proof in tools/.
#ifdef DSP_SAMPLE_FLOAT
#define DSP_SAMPLE float
#else
#define DSP_SAMPLE double
#endif

namespace Rations
{
namespace dsp
{

// Interpolate the 4 provided equispaced points to x in [-1, 2].
//
// VERBATIM from AudioDSPTools dsp/Resample.h. It is a Catmull-Rom spline, and there are
// several algebraically equivalent ways to write one; they do not all round the same way in
// floating point. This is the one the pedals were measured against, so the expression stays
// exactly as it is — nested, with the 0.5 factored out front — and the reformatting below is
// the only difference from the original (clang-format to this project's style).
template <typename T> inline T cubicInterpolation(T p[4], T x)
{
    return p[1] + 0.5 * x *
                      (p[2] - p[0] +
                       x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
                            x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

} // namespace dsp
} // namespace Rations
