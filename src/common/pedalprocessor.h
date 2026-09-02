// PedalProcessor — the audio component every pedal plug-in is built from.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// One template, five instantiations. What a pedal supplies is its traits struct (see
// src/plugins/boost/traits.h); everything below — bus negotiation, the parameter queues, MIDI
// learn, the bypass ramp, the state blob — is the same code in all five binaries, which is the
// only way five separately-released plug-ins stay consistent with each other.
//
// THE SIGNAL PATH is short enough to state in full:
//
//   host float in -> double -> [ dry copy ] -> pedal DSP -> bypass cross-fade vs dry -> float out
//
// The pedal's own footswitch ramp lives inside pedals::Pedal::process and is not repeated here;
// this file's ramp is the HOST's bypass, which is a different switch (see kSwitchId in
// pedalids.h).
//
// REAL-TIME CONTRACT: process() never allocates, locks, does file I/O, logs, or destroys an
// object. Every buffer it touches is sized in setupProcessing.
#pragma once

#include "denormal.h"
#include "midilearn.h"
#include "pedalids.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Rations
{

template <typename Traits> class PedalProcessor : public Steinberg::Vst::AudioEffect
{
public:
    using Dsp = typename Traits::Dsp;
    static constexpr ParamList kParams = Traits::kParams;
    static constexpr int kParamCount = kParams.count;

    PedalProcessor()
    {
        setControllerClass(Traits::controllerUID());
        for (int i = 0; i < kParamCount; ++i)
            mParamNorm[i].store(pedalNorm(kParams[i], kParams[i].def), std::memory_order_relaxed);
        mBypassNorm.store(0.0, std::memory_order_relaxed);
        mMidiBinding.store(0, std::memory_order_relaxed);
        mMidiLearnRow.store(-1, std::memory_order_relaxed);
    }

    static Steinberg::FUnknown *createInstance(void *)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor *>(new PedalProcessor());
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override
    {
        Steinberg::tresult result = AudioEffect::initialize(context);
        if (result != Steinberg::kResultOk)
            return result;

        // The DEFAULT arrangement, which a host is free to renegotiate through
        // setBusArrangements below. A mono-only pedal comes up mono in / mono out so that a host
        // which never negotiates does not silently get a dead right channel.
        addAudioInput(STR16("Input"), Traits::kMonoOnly ? Steinberg::Vst::SpeakerArr::kMono
                                                        : Steinberg::Vst::SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), Traits::kMonoOnly ? Steinberg::Vst::SpeakerArr::kMono
                                                          : Steinberg::Vst::SpeakerArr::kStereo);

        // Without this bus NO MIDI arrives at all — not the notes that come through inputEvents,
        // and not the CC and Program Change that come through the parameter queues either,
        // because a host that sees no event input has no reason to route MIDI here in the first
        // place. One bus, all sixteen channels.
        addEventInput(STR16("MIDI In"), 16);
        return Steinberg::kResultOk;
    }

    //--------------------------------------------------------------------------------------
    // The three arrangements this plug-in accepts, and nothing else. Refusing rather than
    // silently accepting is what makes a host offer a pedal only where it belongs.
    //
    //   mono   -> mono     the DSP runs with a null right channel
    //   mono   -> stereo   a stereo-capable pedal is fed the same signal on both sides and
    //                      widens it; a mono-only pedal runs once and is copied to both outputs
    //   stereo -> stereo   the pedal's native stereo path — refused when kMonoOnly
    //
    // stereo -> mono is refused: a downmix nobody asked for is a worse answer than "this does not
    // fit here", and the host can put a mono instance where it wants one.
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement *inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement *outputs,
                                                     Steinberg::int32 numOuts) override
    {
        namespace Arr = Steinberg::Vst::SpeakerArr;
        if (numIns != 1 || numOuts != 1)
            return Steinberg::kResultFalse;
        const bool inMono = inputs[0] == Arr::kMono;
        const bool inStereo = inputs[0] == Arr::kStereo;
        const bool outMono = outputs[0] == Arr::kMono;
        const bool outStereo = outputs[0] == Arr::kStereo;
        if (!inMono && !inStereo)
            return Steinberg::kResultFalse;
        if (!outMono && !outStereo)
            return Steinberg::kResultFalse;
        if (inStereo && Traits::kMonoOnly)
            return Steinberg::kResultFalse;
        if (inStereo && outMono)
            return Steinberg::kResultFalse;
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override
    {
        return symbolicSampleSize == Steinberg::Vst::kSample32 ? Steinberg::kResultTrue
                                                               : Steinberg::kResultFalse;
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup) override
    {
        Steinberg::tresult result = AudioEffect::setupProcessing(setup);
        if (result != Steinberg::kResultOk)
            return result;

        mSampleRate = setup.sampleRate;
        mMaxBlock = setup.maxSamplesPerBlock;

        // Every allocation this plug-in ever makes on the audio path happens here or inside the
        // pedal's own prepare(), which this calls.
        const size_t n = static_cast<size_t>(std::max(1, mMaxBlock));
        mWorkL.assign(n, 0.0);
        mWorkR.assign(n, 0.0);
        mDryL.assign(n, 0.0);
        mDryR.assign(n, 0.0);
        mDsp.prepare(mSampleRate, std::max(1, mMaxBlock));

        // At least one sample, so the step is finite at any rate and block size.
        mBypassStep = 1.0 / std::max(1.0, kBypassRampMs * 0.001 * mSampleRate);
        return Steinberg::kResultOk;
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override
    {
        if (state) {
            // Clear, so an instance deactivated mid-delay-tail does not resume it seconds later,
            // and come up BYPASSED and ramp in rather than opening mid-effect.
            mDsp.reset();
            mBypassMix = 1.0;
        }
        return AudioEffect::setActive(state);
    }

    //--------------------------------------------------------------------------------------
    Steinberg::uint32 PLUGIN_API getLatencySamples() override
    {
        if constexpr (Traits::kReportsLatency)
            return static_cast<Steinberg::uint32>(std::lround(Dsp::kLatencySamples));
        else
            return 0;
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) override
    {
        setDenormalMode();

        // Bumped before anything reads it, so "the block before this one" is a comparison of two
        // indices rather than a duration. See the press rule in handleParameterChanges.
        ++mBlockIndex;

        // Anything the MIDI table makes this plug-in do to itself is collected here and reported
        // before the first early return below, so a message landing on a block with no audio in
        // it is not silently dropped.
        mEchoCount = 0;
        handleParameterChanges(data.inputParameterChanges);
        handleInputEvents(data.inputEvents);
        for (int i = 0; i < mEchoCount; ++i)
            writeOutputPoint(data.outputParameterChanges, mEcho[i].id, mEcho[i].value);

        if constexpr (Traits::kWantsTempo) {
            // A host is entitled to supply no ProcessContext at all, and one that does is only
            // claiming a tempo when it says so in the flags
            // (pluginterfaces/vst/ivstprocesscontext.h).
            double bpm = 0.0;
            if (data.processContext &&
                (data.processContext->state & Steinberg::Vst::ProcessContext::kTempoValid))
                bpm = data.processContext->tempo;
            mDsp.setTempo(bpm);
        }

        const Steinberg::int32 n = data.numSamples;
        if (n <= 0 || data.numInputs < 1 || data.numOutputs < 1)
            return Steinberg::kResultOk;
        if (!data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
            return Steinberg::kResultOk;

        const Steinberg::int32 inCh = data.inputs[0].numChannels;
        const Steinberg::int32 outCh = data.outputs[0].numChannels;
        if (inCh < 1 || outCh < 1 || n > mMaxBlock)
            return Steinberg::kResultOk;

        float **in = data.inputs[0].channelBuffers32;
        float **out = data.outputs[0].channelBuffers32;

        // Denormalize the parameters once per block. setParams may compute filter coefficients,
        // which is why it is not called per sample; it may not allocate, which is the contract
        // pedals::Pedal states.
        double plain[kParamCount];
        for (int i = 0; i < kParamCount; ++i)
            plain[i] = pedalPlain(kParams[i], mParamNorm[i].load(std::memory_order_relaxed));
        mDsp.setEngaged(plain[0] > 0.5);
        mDsp.setParams(plain);

        // WHAT THE DSP SEES. A mono-only pedal always runs with a null right channel; a stereo
        // pedal runs with one whenever there is a stereo OUTPUT to put it in, which means a mono
        // input is duplicated first and the pedal decorrelates the two sides itself. That is the
        // difference between a chorus that widens a mono guitar and one that merely doubles it.
        const bool stereoDsp = !Traits::kMonoOnly && outCh >= 2;

        for (Steinberg::int32 i = 0; i < n; ++i)
            mWorkL[static_cast<size_t>(i)] = static_cast<double>(in[0][i]);
        if (stereoDsp) {
            const float *src = (inCh >= 2) ? in[1] : in[0];
            for (Steinberg::int32 i = 0; i < n; ++i)
                mWorkR[static_cast<size_t>(i)] = static_cast<double>(src[i]);
        }

        // The dry copy the bypass ramp mixes against. Taken from the WORK buffers rather than the
        // host's, because the host is allowed to hand the same pointer in and out.
        std::copy(mWorkL.begin(), mWorkL.begin() + n, mDryL.begin());
        if (stereoDsp)
            std::copy(mWorkR.begin(), mWorkR.begin() + n, mDryR.begin());

        mDsp.process(mWorkL.data(), stereoDsp ? mWorkR.data() : nullptr, static_cast<int>(n));

        // The HOST's bypass, ramped. Unlike the footswitch this does not reset anything: a host
        // bypass is expected to be transparent and reversible, and a tail that vanished and came
        // back from silence would not be.
        const double target = mBypassNorm.load(std::memory_order_relaxed) > 0.5 ? 1.0 : 0.0;
        double mix = mBypassMix;
        for (Steinberg::int32 i = 0; i < n; ++i) {
            if (mix < target)
                mix = std::min(target, mix + mBypassStep);
            else if (mix > target)
                mix = std::max(target, mix - mBypassStep);
            const size_t k = static_cast<size_t>(i);
            mWorkL[k] = (1.0 - mix) * mWorkL[k] + mix * mDryL[k];
            if (stereoDsp)
                mWorkR[k] = (1.0 - mix) * mWorkR[k] + mix * mDryR[k];
        }
        mBypassMix = mix;

        // Out. A stereo output fed by a mono DSP gets the one result on both sides, which is what
        // a mono pedal into a stereo rig sounds like; channels beyond the second (a host may hand
        // over more than were negotiated) are silenced rather than left holding stale audio.
        for (Steinberg::int32 i = 0; i < n; ++i)
            out[0][i] = static_cast<float>(mWorkL[static_cast<size_t>(i)]);
        if (outCh >= 2) {
            const std::vector<double> &right = stereoDsp ? mWorkR : mWorkL;
            for (Steinberg::int32 i = 0; i < n; ++i)
                out[1][i] = static_cast<float>(right[static_cast<size_t>(i)]);
        }
        for (Steinberg::int32 c = 2; c < outCh; ++c)
            std::memset(out[c], 0, static_cast<size_t>(n) * sizeof(float));

        return Steinberg::kResultOk;
    }

    //--------------------------------------------------------------------------------------
    // State. Length-prefixed, every read bounds-checked, and a malformed blob loads NOTHING
    // rather than half of itself: a project that opens with three of five knobs moved is worse
    // than one that opens at the defaults and says so.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) override
    {
        if (!state)
            return Steinberg::kResultFalse;
        Steinberg::IBStreamer s(state, kLittleEndian);

        Steinberg::int32 version = 0;
        if (!s.readInt32(version) || version < 1)
            return Steinberg::kResultFalse;

        Steinberg::int32 count = 0;
        if (!s.readInt32(count) || count < 0 || count > kParamStateMax)
            return Steinberg::kResultFalse;

        double values[kParamStateMax];
        for (Steinberg::int32 i = 0; i < count; ++i)
            if (!s.readDouble(values[i]))
                return Steinberg::kResultFalse;

        Steinberg::int32 binding = 0;
        double bypass = 0.0;
        if (!s.readInt32(binding) || !s.readDouble(bypass))
            return Steinberg::kResultFalse;

        // Only now, with everything read and nothing having failed, is anything published.
        // A blob written by a build with MORE controls than this one keeps the ones this build
        // knows and drops the rest; one with fewer leaves the newer controls at their defaults.
        for (Steinberg::int32 i = 0; i < count && i < kParamCount; ++i)
            mParamNorm[i].store(clamp01(values[i]), std::memory_order_relaxed);
        // Every word goes through unpackBinding, which is where an out-of-range message type or
        // channel becomes "not learned" instead of a row matching something nobody can name.
        mMidiBinding.store(packBinding(unpackBinding(static_cast<std::uint32_t>(binding))),
                           std::memory_order_release);
        mMidiLearnRow.store(-1, std::memory_order_release);
        mBypassNorm.store(clamp01(bypass), std::memory_order_relaxed);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) override
    {
        if (!state)
            return Steinberg::kResultFalse;
        Steinberg::IBStreamer s(state, kLittleEndian);
        s.writeInt32(kStateVersion);
        s.writeInt32(kParamCount);
        for (int i = 0; i < kParamCount; ++i)
            s.writeDouble(mParamNorm[i].load(std::memory_order_relaxed));
        s.writeInt32(static_cast<Steinberg::int32>(mMidiBinding.load(std::memory_order_acquire)));
        s.writeDouble(mBypassNorm.load(std::memory_order_relaxed));
        return Steinberg::kResultOk;
    }

    //--------------------------------------------------------------------------------------
    // MIDI learn's control channel. Message thread throughout, on state the audio thread also
    // reads — which is why every write is a single atomic store of a packed word rather than an
    // edit of a struct.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) override
    {
        if (!message)
            return Steinberg::kInvalidArgument;
        const char *id = message->getMessageID();

        if (id && strcmp(id, kMsgMidiLearn) == 0) {
            Steinberg::int64 row = -1;
            if (message->getAttributes()->getInt(kMidiRowAttr, row) != Steinberg::kResultOk)
                row = -1;
            const bool valid = row >= 0 && row < kMidiLearnRowCount;
            mMidiLearnRow.store(valid ? static_cast<int>(row) : -1, std::memory_order_release);
            sendMidiTable();
            return Steinberg::kResultOk;
        }
        if (id && strcmp(id, kMsgMidiClear) == 0) {
            Steinberg::int64 row = -1;
            if (message->getAttributes()->getInt(kMidiRowAttr, row) == Steinberg::kResultOk &&
                row >= 0 && row < kMidiLearnRowCount) {
                mMidiBinding.store(0, std::memory_order_release);
                // Clearing the row that is listening also stops it listening: the user has just
                // said what they want that row to be, and it is nothing.
                int armed = static_cast<int>(row);
                mMidiLearnRow.compare_exchange_strong(armed, -1, std::memory_order_release,
                                                      std::memory_order_relaxed);
            }
            sendMidiTable();
            return Steinberg::kResultOk;
        }
        if (id && strcmp(id, kMsgRequestMidi) == 0) {
            sendMidiTable();
            return Steinberg::kResultOk;
        }
        return AudioEffect::notify(message);
    }

private:
    static double clamp01(double v)
    {
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }

    // A parameter the plug-in changed BY ITSELF has to be reported, or the host's automation lane
    // and the editor's copy end up disagreeing with the audio.
    static void writeOutputPoint(Steinberg::Vst::IParameterChanges *outChanges,
                                 Steinberg::Vst::ParamID id, double value)
    {
        if (!outChanges)
            return;
        Steinberg::int32 queueIndex = 0;
        Steinberg::Vst::IParamValueQueue *queue = outChanges->addParameterData(id, queueIndex);
        if (!queue)
            return;
        Steinberg::int32 pointIndex = 0;
        queue->addPoint(0, value, pointIndex);
    }

    void sendMidiTable()
    {
        Steinberg::IPtr<Steinberg::Vst::IMessage> msg = owned(allocateMessage());
        if (!msg)
            return;
        msg->setMessageID(kMsgMidiTable);
        const std::uint32_t word = mMidiBinding.load(std::memory_order_acquire);
        msg->getAttributes()->setBinary(kMidiTableAttr, &word, sizeof(word));
        msg->getAttributes()->setInt(kMidiArmedAttr, mMidiLearnRow.load(std::memory_order_acquire));
        sendMessage(msg);
    }

    //--------------------------------------------------------------------------------------
    void handleParameterChanges(Steinberg::Vst::IParameterChanges *changes)
    {
        if (!changes)
            return;
        const Steinberg::int32 count = changes->getParameterCount();
        for (Steinberg::int32 q = 0; q < count; ++q) {
            Steinberg::Vst::IParamValueQueue *queue = changes->getParameterData(q);
            if (!queue)
                continue;
            const Steinberg::int32 points = queue->getPointCount();
            if (points <= 0)
                continue;
            // The LAST point in the block is the value this block runs at. Sample-accurate
            // automation of a knob is not something a stompbox needs, and interpolating it would
            // mean recomputing coefficients per sample.
            Steinberg::int32 offset = 0;
            Steinberg::Vst::ParamValue value = 0.0;
            if (queue->getPoint(points - 1, offset, value) != Steinberg::kResultTrue)
                continue;
            const Steinberg::Vst::ParamID id = queue->getParameterId();

            if (id >= kMidiCcIdFirst && id < kMidiCcIdFirst + kMidiCcCount) {
                handleCc(static_cast<int>(id - kMidiCcIdFirst), value);
                continue;
            }
            if (id == kMidiProgramListId) {
                handleProgramChange(value);
                continue;
            }
            if (id == kBypassId) {
                mBypassNorm.store(clamp01(value), std::memory_order_relaxed);
                continue;
            }
            const int index = paramIndex(kParams, id);
            if (index >= 0)
                mParamNorm[index].store(clamp01(value), std::memory_order_relaxed);
        }
    }

    // Control Change. See midilearn.h for why a press is any value at or above 64 rather than a
    // rising edge, and why the repeat guard is a block index rather than a clock.
    void handleCc(int cc, double value)
    {
        const int ccValue = std::clamp(static_cast<int>(std::lround(value * 127.0)), 0, 127);
        const int last = mCcLast[cc];
        const std::uint32_t lastBlock = mCcLastBlock[cc];
        mCcLast[cc] = static_cast<std::uint8_t>(ccValue);
        mCcLastBlock[cc] = mBlockIndex;

        // LEARNING asks a different question from performing, and gating it on the same rule is a
        // bug in the most ordinary re-mapping there is: moving a footswitch that is already bound.
        // That switch was last seen pressed, so while a row listens ANY change is an answer — and
        // so is a press, which covers a controller that keeps sending one value.
        const bool learning = mMidiLearnRow.load(std::memory_order_relaxed) >= 0;
        const bool resent = ccValue == last && lastBlock + 1 == mBlockIndex && lastBlock != 0;
        const bool fire =
            learning ? (ccValue != last || ccValue >= 64) : (ccValue >= 64 && !resent);
        if (fire)
            midiTrigger(MidiMsg::ControlChange, kMidiAnyChannel, cc);
    }

    // Program Change arrives as the program list's own parameter, so its value is the program
    // number over the list's step count — 127 steps for 128 programs. There is no release, so
    // every message is a press; the only thing to suppress is the same program arriving again in
    // the very next block, which is a host resending rather than a second stamp.
    void handleProgramChange(double value)
    {
        const int program = std::clamp(static_cast<int>(std::lround(value * 127.0)), 0, 127);
        const bool resent = program == mPcLast && mPcLastBlock + 1 == mBlockIndex;
        mPcLast = program;
        mPcLastBlock = mBlockIndex;
        if (!resent)
            midiTrigger(MidiMsg::ProgramChange, kMidiAnyChannel, program);
    }

    // Note On, the one of the three message types that arrives as an actual MIDI event and the
    // one that therefore still knows which MIDI channel it came from.
    void handleInputEvents(Steinberg::Vst::IEventList *events)
    {
        if (!events)
            return;
        const Steinberg::int32 count = events->getEventCount();
        for (Steinberg::int32 i = 0; i < count; ++i) {
            Steinberg::Vst::Event e = {};
            if (events->getEvent(i, e) != Steinberg::kResultOk)
                continue;
            if (e.type != Steinberg::Vst::Event::kNoteOnEvent)
                continue;
            // A note on with zero velocity is a note OFF — the oldest convention in MIDI, and one
            // a controller is entitled to use. Acting on it would toggle the pedal again when the
            // foot came up.
            if (e.noteOn.velocity <= 0.0f)
                continue;
            midiTrigger(MidiMsg::NoteOn, e.noteOn.channel, e.noteOn.pitch);
        }
    }

    // One decoded message. Audio thread: no allocation, no lock, no destructor, and no call into
    // the controller — what the plug-in does to itself here is reported to the host through the
    // output parameter queue.
    void midiTrigger(MidiMsg msg, int channel, int data1)
    {
        MidiBinding incoming;
        incoming.msg = msg;
        incoming.channel = channel;
        incoming.data1 = data1;
        const std::uint32_t word = packBinding(incoming);

        if (mMidiLearnRow.load(std::memory_order_relaxed) >= 0) {
            mMidiBinding.store(word, std::memory_order_release);
            mMidiLearnRow.store(-1, std::memory_order_release);
            // The press that taught the row does not also perform it: learning a footswitch would
            // otherwise turn the pedal on as a side effect of being taught, while the user's
            // attention is on the pedal at their feet.
            return;
        }

        const MidiBinding bound = unpackBinding(mMidiBinding.load(std::memory_order_acquire));
        if (!bindingMatches(bound, msg, channel, data1))
            return;

        // The footswitch toggles. What is ECHOED is the value actually stored, never the row's
        // own — for a toggle the two are not the same thing, and reporting the wrong one would
        // leave the host's lane and the lamp disagreeing with the pedal that is audibly running.
        const double now = mParamNorm[0].load(std::memory_order_relaxed);
        const double performed = now > 0.5 ? 0.0 : 1.0;
        mParamNorm[0].store(performed, std::memory_order_relaxed);
        if (mEchoCount < kEchoMax) {
            mEcho[mEchoCount].id = kMidiLearnRows[0].param;
            mEcho[mEchoCount].value = performed;
            ++mEchoCount;
        }
    }

    //--------------------------------------------------------------------------------------
    Dsp mDsp;

    double mSampleRate = 48000.0;
    int mMaxBlock = 0;
    std::vector<double> mWorkL, mWorkR, mDryL, mDryR;

    double mBypassMix = 1.0; // 1 = fully bypassed; setActive comes up here and ramps in
    double mBypassStep = 1.0;

    std::atomic<double> mParamNorm[kParamCount];
    std::atomic<double> mBypassNorm{0.0};

    std::atomic<std::uint32_t> mMidiBinding{0};
    std::atomic<int> mMidiLearnRow{-1};

    std::uint32_t mBlockIndex = 0;
    std::uint8_t mCcLast[kMidiCcCount] = {};
    std::uint32_t mCcLastBlock[kMidiCcCount] = {};
    int mPcLast = -1;
    std::uint32_t mPcLastBlock = 0;

    // At most one row can fire per block, and only one parameter follows from it. Two, so a
    // future second row does not silently drop its echo.
    static constexpr int kEchoMax = 2;
    struct Echo {
        Steinberg::Vst::ParamID id = 0;
        double value = 0.0;
    };
    Echo mEcho[kEchoMax];
    int mEchoCount = 0;
};

} // namespace Rations
