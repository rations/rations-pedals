// Rations Pedals — the base every pedal is built on.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// A pedal is a DSP block compiled into one of these plug-ins, driven by that plug-in's own
// parameters. Nothing here hosts anyone else's plug-in, and nothing here is a nested VST3.
//
// ONE BASE SERVES BOTH BUS ARRANGEMENTS. `process` takes a right channel that MAY BE NULL, and a
// pedal running on a mono track simply never looks at it — so the Boost, which is mono-only
// because a Tube Screamer is one circuit, and the Reverb, which is true stereo, are the same kind
// of object. Splitting this into MonoPedal and StereoPedal would duplicate the engage ramp, which
// is the only thing the base actually does.
//
// THE REAL-TIME CONTRACT, which is the whole reason this class exists rather than five loose
// classes: prepare() does every allocation, and process(), setParams() and setEngaged() never
// allocate, lock, log, block or destroy. A pedal that needs a buffer sizes it in prepare().
#pragma once

// DSP_SAMPLE (a double). Lifted out of AudioDSPTools into this project's own header when
// the pedals left the amp behind them — see the note at the top of dsp/sample.h for why the
// two lines it carries are reproduced rather than depended on.
#include "dsp/sample.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Rations
{
namespace pedals
{

// How long a footswitch takes to cross-fade its pedal in or out. Long enough that neither the
// engage nor the disengage is a step — a hard mix change is a click in exactly the way a hard
// model swap is — and short enough that a stomp still reads as instant. The same reasoning, and
// nearly the same number, as the channel fade.
inline constexpr double kEngageMs = 8.0;

class Pedal
{
public:
    virtual ~Pedal() = default;

    // The only allocating call. Sizes the dry buffers this base needs and then the pedal's own.
    void prepare(double sampleRate, int maxBlock)
    {
        mSampleRate = sampleRate;
        mMaxBlock = maxBlock;
        mDryL.assign(static_cast<size_t>(maxBlock), 0.0);
        mDryR.assign(static_cast<size_t>(maxBlock), 0.0);
        // A ramp measured in milliseconds, so it is the same audible length at every rate.
        mMixStep = 1.0 / std::max(1.0, kEngageMs * 0.001 * sampleRate);
        prepareImpl(sampleRate, maxBlock);
        reset();
    }

    // Clears state without reallocating. Safe from the audio thread.
    void reset()
    {
        mMix = mEngaged ? 1.0 : 0.0;
        resetImpl();
    }

    // The footswitch. Only ever sets a target; the crossing is done a sample at a time in
    // process(), because a footswitch that stepped the mix would click.
    void setEngaged(bool on)
    {
        mEngaged = on;
    }
    bool engaged() const
    {
        return mEngaged;
    }

    // Whether this pedal still has work to do. NOT the same as engaged(): a pedal switched off a
    // moment ago is still ramping out, and a caller that skipped it on engaged() alone would cut
    // the fade off part-way, which is the click the fade exists to prevent. Goes false only once
    // the ramp has actually landed on zero.
    bool active() const
    {
        return mEngaged || mMix > 0.0;
    }

    // This pedal's own slice of the denormalized parameter array, in table order. Called
    // once per sub-block from the audio thread, so it may compute coefficients but must not
    // allocate.
    virtual void setParams(const double *plain) = 0;

    // `r` is null on a mono chain. In place: the pedal reads and writes the same buffers.
    void process(DSP_SAMPLE *l, DSP_SAMPLE *r, int numSamples)
    {
        const double target = mEngaged ? 1.0 : 0.0;

        // Fully out, and the ramp has finished: do nothing at all, and do not charge the audio
        // thread for a pedal that is switched off.
        //
        // The state a pedal loses by being reset is worth nothing to keep: it is a delay line and
        // an LFO phase, and a real true-bypass pedal loses both when it is switched out. So a
        // disengaged pedal is reset once, at the moment its ramp reaches zero, and then skipped —
        // which is both what the hardware does and what leaves a switched-off pedal costing the
        // audio thread nothing.
        //
        // This is deliberately NOT what the HOST's bypass does. kBypassId takes the whole plug-in
        // out of circuit and is the host's to automate; this is the stompbox's own footswitch, and
        // the two are different events. See the note on kSwitchId in common/pedalids.h.
        if (!mEngaged && mMix <= 0.0) {
            if (mNeedsReset) {
                resetImpl();
                mNeedsReset = false;
            }
            return;
        }
        mNeedsReset = true;

        const size_t n = static_cast<size_t>(numSamples);
        std::copy(l, l + n, mDryL.begin());
        if (r)
            std::copy(r, r + n, mDryR.begin());

        processImpl(l, r, numSamples);

        // Crossfade wet against the dry copy. Endpoints are SNAPPED to exact 0.0 and 1.0 rather
        // than left as accumulated residue: residue would leave a pedal permanently a hair short
        // of fully in or fully out, and the skip above would then never trigger.
        double mix = mMix;
        for (int i = 0; i < numSamples; ++i) {
            if (mix < target)
                mix = std::min(target, mix + mMixStep);
            else if (mix > target)
                mix = std::max(target, mix - mMixStep);
            const size_t k = static_cast<size_t>(i);
            l[i] = mix * l[i] + (1.0 - mix) * mDryL[k];
            if (r)
                r[i] = mix * r[i] + (1.0 - mix) * mDryR[k];
        }
        mMix = mix;
    }

protected:
    virtual void prepareImpl(double sampleRate, int maxBlock) = 0;
    virtual void resetImpl() = 0;
    // In place, and free to ignore the engage state entirely — the base does that mixing.
    virtual void processImpl(DSP_SAMPLE *l, DSP_SAMPLE *r, int numSamples) = 0;

    double mSampleRate = 48000.0;
    int mMaxBlock = 0;

private:
    std::vector<DSP_SAMPLE> mDryL, mDryR;
    double mMix = 0.0;
    double mMixStep = 1.0;
    bool mEngaged = false;
    bool mNeedsReset = false;
};

} // namespace pedals
} // namespace Rations
