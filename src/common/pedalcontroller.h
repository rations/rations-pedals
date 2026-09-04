// PedalController — the edit controller every pedal plug-in is built from.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// EditControllerEx1 rather than EditController, and IMidiMapping alongside it, both for the same
// reason: a footswitch. IMidiMapping is how Control Change reaches a VST3 plug-in at all, and
// IUnitInfo (which EditControllerEx1 supplies) is how Program Change does — see midilearn.h for
// the SDK sites. Nothing about a pedal's own four knobs needed either.
//
// The MIDI binding is held here as a MIRROR of the processor's, refreshed by the kMsgMidiTable
// message. The processor is the authority: it is the half that writes the state blob and the half
// the audio thread reads.
#pragma once

#include "midilearn.h"
#include "pedalids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cstdio>
#include <cstring>

namespace Rations
{

template <typename Traits> class PedalView;

template <typename Traits>
class PedalController : public Steinberg::Vst::EditControllerEx1,
                        public Steinberg::Vst::IMidiMapping
{
public:
    static constexpr ParamList kParams = Traits::kParams;
    static constexpr int kParamCount = kParams.count;

    static Steinberg::FUnknown *createInstance(void *)
    {
        return static_cast<Steinberg::Vst::IEditController *>(new PedalController());
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        tresult result = EditControllerEx1::initialize(context);
        if (result != kResultOk)
            return result;

        // FIRST, before any parameter and before any other unit. See kMidiUnitId in pedalids.h
        // for the measurement behind that.
        addUnit(new Unit(STR16("MIDI"), kMidiUnitId, kRootUnitId, kMidiProgramListId));

        // The host's bypass, drawn as the bat toggle on the enclosure. kIsBypass is what makes a
        // host's own bypass button drive this one rather than putting a second switch beside it.
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);

        // The pedal's own controls, declared by walking its table rather than written out by
        // hand, because that same table is what the processor stores, what the state blob is
        // written in and what the editor draws. See pedalids.h.
        for (int i = 0; i < kParamCount; ++i) {
            const PedalParamSpec &spec = kParams[i];
            String128 title = {};
            UString(title, USTRINGSIZE(title)).fromAscii(spec.title);
            String128 unitName = {};
            if (spec.unit)
                UString(unitName, USTRINGSIZE(unitName)).fromAscii(spec.unit);
            const TChar *units = spec.unit ? unitName : nullptr;

            switch (spec.kind) {
                case PedalParamKind::Toggle:
                    // Step count 1, and addParameter's default argument is NORMALIZED — which for
                    // a toggle is the same 0 or 1. The footswitch defaults OFF: a fresh instance
                    // is a pedal that has been placed in the chain but not yet stamped on.
                    parameters.addParameter(title, nullptr, 1, spec.def,
                                            ParameterInfo::kCanAutomate, spec.id);
                    break;
                case PedalParamKind::List: {
                    // Only the Delay's sync division. A host stores the normalized position, so
                    // appending to kDelaySyncNames is safe and reordering it is not.
                    auto *list = new StringListParameter(title, spec.id, nullptr,
                                                         ParameterInfo::kCanAutomate |
                                                             ParameterInfo::kIsList);
                    for (int k = 0; k < kDelaySyncCount; ++k) {
                        String128 entry = {};
                        UString(entry, USTRINGSIZE(entry)).fromAscii(kDelaySyncNames[k]);
                        list->appendString(entry);
                    }
                    list->setNormalized(pedalNorm(spec, spec.def));
                    parameters.addParameter(list);
                    break;
                }
                case PedalParamKind::Range: {
                    auto *range = new RangeParameter(title, spec.id, units, spec.min, spec.max,
                                                     spec.def, 0, ParameterInfo::kCanAutomate);
                    range->setPrecision(spec.precision);
                    parameters.addParameter(range);
                    break;
                }
            }
        }

        // --- the MIDI block ---------------------------------------------------------------
        //
        // 129 parameters nobody will ever turn. They are here because a footswitch's messages do
        // not arrive as MIDI: Control Change arrives as a parameter change routed by IMidiMapping,
        // and Program Change as a parameter change on a kIsProgramChange parameter found through
        // IUnitInfo. Both need a real parameter to land on or they do not arrive at all.
        //
        // FLAGS 0, and this is not an oversight. kIsHidden would be the obvious choice for a
        // parameter with no UI, and the SDK documents it as implying kIsReadOnly — which would
        // make these unwritable and silently discard every CC the host routed to them. Flags 0 is
        // the SDK's own pattern for MIDI-mapped parameters (mdaJX10Controller.cpp).
        for (int cc = 0; cc < kMidiCcCount; ++cc) {
            char ascii[32];
            snprintf(ascii, sizeof(ascii), "MIDI CC %d", cc);
            String128 title = {};
            UString(title, USTRINGSIZE(title)).fromAscii(ascii);
            parameters.addParameter(title, nullptr, 0, 0.0, 0,
                                    static_cast<ParamID>(kMidiCcIdFirst + cc), kMidiUnitId);
        }

        // Program Change. A program LIST rather than a plain parameter, because that is the only
        // route a Program Change actually travels: the host looks up the unit for the incoming
        // MIDI channel, finds that unit's program list, and writes the program number onto the
        // list's kIsProgramChange parameter. ProgramList builds that parameter itself, with the
        // list id as the ParamID — which is why kMidiProgramListId is the id it is.
        //
        // The programs are named for what they are: the message, not a preset. This plug-in has
        // no presets and the list is not pretending to be one. It is the doorway a PC number
        // comes through, and the learn table decides what any given number does.
        auto *programs = new ProgramList(STR16("Program Change"), kMidiProgramListId, kMidiUnitId);
        for (int pc = 0; pc < kMidiProgramCount; ++pc) {
            char ascii[32];
            snprintf(ascii, sizeof(ascii), "PC %d", pc);
            String128 name = {};
            UString(name, USTRINGSIZE(name)).fromAscii(ascii);
            programs->addProgram(name);
        }
        addProgramList(programs);
        if (Parameter *pcParam = programs->getParameter())
            parameters.addParameter(pcParam);

        return kResultOk;
    }

    //--------------------------------------------------------------------------------------
    // The processor's blob, read with exactly the reader in PedalProcessor::setState — including
    // its refusal to apply a partial read. See the note there.
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state) SMTG_OVERRIDE
    {
        using namespace Steinberg;
        if (!state)
            return kResultFalse;
        IBStreamer s(state, kLittleEndian);

        int32 version = 0;
        if (!s.readInt32(version) || version < 1)
            return kResultFalse;
        int32 count = 0;
        if (!s.readInt32(count) || count < 0 || count > kParamStateMax)
            return kResultFalse;
        // Non-finite values are refused here for the same reason and on the same terms as in
        // PedalProcessor::setState — the two readers must accept and reject exactly the same
        // bytes, or the panel and the audio disagree about what loaded.
        double values[kParamStateMax];
        for (int32 i = 0; i < count; ++i)
            if (!s.readDouble(values[i]) || !dsp::isFinite(values[i]))
                return kResultFalse;
        int32 binding = 0;
        double bypass = 0.0;
        if (!s.readInt32(binding) || !s.readDouble(bypass) || !dsp::isFinite(bypass))
            return kResultFalse;

        for (int32 i = 0; i < count && i < kParamCount; ++i)
            setParamNormalized(kParams[i].id, values[i]);
        setParamNormalized(kBypassId, bypass);
        mBinding = unpackBinding(static_cast<std::uint32_t>(binding));
        mArmedRow = -1;
        return kResultOk;
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API queryInterface(const char *iid, void **obj) SMTG_OVERRIDE
    {
        QUERY_INTERFACE(iid, obj, Steinberg::Vst::IMidiMapping::iid, Steinberg::Vst::IMidiMapping)
        return EditControllerEx1::queryInterface(iid, obj);
    }
    DELEGATE_REFCOUNT(Steinberg::Vst::EditControllerEx1)

    //--------------------------------------------------------------------------------------
    // Controller numbers 0..127 only. 128 and 129 are aftertouch and pitch bend, which are not
    // switches and have nothing to learn; 130 and up are not controller numbers at all — the
    // SDK's kCountCtrlNumber is 130, the same value as kCtrlProgramChange, because everything
    // from there up belongs to the legacy MIDI-CC-OUT namespace. Answering for those would be
    // answering a question the host is not asking, and the SDK's own validator says so.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
        Steinberg::Vst::CtrlNumber ccNumber, Steinberg::Vst::ParamID &id) SMTG_OVERRIDE
    {
        if (busIndex != 0 || ccNumber < 0 || ccNumber >= kMidiCcCount)
            return Steinberg::kResultFalse;
        id = static_cast<Steinberg::Vst::ParamID>(kMidiCcIdFirst + ccNumber);
        return Steinberg::kResultTrue;
    }

    // Every channel of the one event input, onto the one unit that has a program list. There is
    // no per-channel behaviour to express: a Program Change means the same thing whichever
    // channel the controller sends it on, which is the same limitation CC has and for the same
    // reason (midilearn.h).
    Steinberg::tresult PLUGIN_API getUnitByBus(Steinberg::Vst::MediaType type,
                                               Steinberg::Vst::BusDirection dir,
                                               Steinberg::int32 busIndex, Steinberg::int32 channel,
                                               Steinberg::Vst::UnitID &unitId) SMTG_OVERRIDE
    {
        if (type == Steinberg::Vst::kEvent && dir == Steinberg::Vst::kInput && busIndex == 0 &&
            channel >= 0 && channel < 16) {
            unitId = kMidiUnitId;
            return Steinberg::kResultTrue;
        }
        return Steinberg::kResultFalse;
    }

    //--------------------------------------------------------------------------------------
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) SMTG_OVERRIDE
    {
        if (!message)
            return Steinberg::kInvalidArgument;
        const char *id = message->getMessageID();
        if (id && strcmp(id, kMsgMidiTable) == 0) {
            const void *data = nullptr;
            Steinberg::uint32 size = 0;
            if (message->getAttributes()->getBinary(kMidiTableAttr, data, size) ==
                    Steinberg::kResultOk &&
                data && size == sizeof(std::uint32_t)) {
                std::uint32_t word = 0;
                memcpy(&word, data, sizeof(word));
                mBinding = unpackBinding(word);
            }
            Steinberg::int64 armed = -1;
            message->getAttributes()->getInt(kMidiArmedAttr, armed);
            mArmedRow = (armed >= 0 && armed < kMidiLearnRowCount) ? static_cast<int>(armed) : -1;
            notifyMidiTable();
            return Steinberg::kResultOk;
        }
        return EditControllerEx1::notify(message);
    }

    //--------------------------------------------------------------------------------------
    // Overridden so the editor is told about EVERY route into a parameter — automation, a generic
    // UI, a state load and the processor's own MIDI-driven footswitch all pass through here, and
    // an editor that only watched its own clicks would be wrong after any of them.
    Steinberg::tresult PLUGIN_API
    setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

    // The native editor. The live view is tracked through the EditorView attach hooks; it runs on
    // the host's UI/run-loop thread, as does everything that pushes to it, so mView needs no lock.
    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    void editorAttached(Steinberg::Vst::EditorView *editor) SMTG_OVERRIDE;
    void editorRemoved(Steinberg::Vst::EditorView *editor) SMTG_OVERRIDE;

    //--------------------------------------------------------------------------------------
    // What the strip under the enclosure calls. All message-thread, all forwarded to the
    // processor, which is the half that owns the table.
    void armMidiLearn(int row)
    {
        if (row < -1 || row >= kMidiLearnRowCount)
            return;
        mArmedRow = row; // optimistic, so the button reads "Listening" on the next frame
        notifyMidiTable();
        sendMidiRow(kMsgMidiLearn, row);
    }
    void clearMidiLearn(int row)
    {
        if (row < 0 || row >= kMidiLearnRowCount)
            return;
        sendMidiRow(kMsgMidiClear, row);
    }
    // Called when the editor opens: the binding was learned before this view existed.
    void requestMidiTable()
    {
        sendMidiRow(kMsgRequestMidi, -1);
    }

    int armedMidiRow() const
    {
        return mArmedRow;
    }
    const MidiBinding &midiBinding() const
    {
        return mBinding;
    }

private:
    void sendMidiRow(const char *messageId, int row)
    {
        Steinberg::IPtr<Steinberg::Vst::IMessage> msg = owned(allocateMessage());
        if (!msg)
            return;
        msg->setMessageID(messageId);
        msg->getAttributes()->setInt(kMidiRowAttr, row);
        sendMessage(msg);
    }

    void notifyMidiTable();

    MidiBinding mBinding;
    int mArmedRow = -1;
    PedalView<Traits> *mView = nullptr;
};

} // namespace Rations

// The view's definition needs the controller's, and the controller's out-of-line members need the
// view's. Included here rather than at the top so that either header may be included first.
#include "pedalview.h"

namespace Rations
{

template <typename Traits>
Steinberg::tresult PLUGIN_API PedalController<Traits>::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value)
{
    const Steinberg::tresult r = EditControllerEx1::setParamNormalized(tag, value);
    if (mView)
        mView->ParamChanged(tag, value);
    return r;
}

template <typename Traits>
Steinberg::IPlugView *PLUGIN_API PedalController<Traits>::createView(Steinberg::FIDString name)
{
    if (name && strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
        return new PedalView<Traits>(this);
    return nullptr;
}

template <typename Traits>
void PedalController<Traits>::editorAttached(Steinberg::Vst::EditorView *editor)
{
    mView = static_cast<PedalView<Traits> *>(editor);
    // The editor was created after the parameters were, so it holds defaults. Push the real
    // values at it before its first frame, or it opens showing a pedal nobody set.
    for (int i = 0; i < kParamCount; ++i)
        mView->ParamChanged(kParams[i].id, getParamNormalized(kParams[i].id));
    mView->ParamChanged(kBypassId, getParamNormalized(kBypassId));
}

template <typename Traits>
void PedalController<Traits>::editorRemoved(Steinberg::Vst::EditorView *editor)
{
    if (mView == editor)
        mView = nullptr;
}

template <typename Traits> void PedalController<Traits>::notifyMidiTable()
{
    if (mView)
        mView->MidiTableChanged();
}

} // namespace Rations
