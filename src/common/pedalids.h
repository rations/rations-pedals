// Rations Pedals — the parameter vocabulary every pedal is described in.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// ONE TABLE PER PEDAL, and four readers of it: the controller declares parameters from it, the
// processor keeps a dense normalized array indexed the same way, the editor lays a face out from
// it, and tools/pedalcheck sweeps it. Adding a knob to a pedal is one line here and nothing
// anywhere else — which is the whole reason the enclosure art is blank.
//
// WHY THE FIVE TABLES LIVE IN ONE HEADER when the five plug-ins are separate binaries. They are
// five instances of one idea, and the thing that keeps them consistent — the same footswitch id,
// the same knob numbering, the same normalization — is only checkable if they are written down
// together. A plug-in compiles the whole header and links one table; nothing is shared at runtime.
//
// PARAMETER IDS ARE THE SAME NUMBERS IN ALL FIVE. In rations-amp all twenty-four pedal controls
// shared one host parameter list, so each needed its own id and its own disambiguated title
// ("Boost Drive"). Here each plug-in has a list to itself, so id 301 is "the first knob" in every
// one of them and the title is the word printed on the enclosure. That is a promise about a
// NUMBER, and it is kept the way rations-amp keeps its: entries may be APPENDED to a pedal, never
// reordered and never renumbered, or an old project's values land on the wrong control.
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

#include <cmath>
#include <cstddef>

namespace Rations
{

enum ParamIds : Steinberg::Vst::ParamID {
    // The host's own bypass, drawn as the bat toggle above the lamp. NOT the footswitch: see the
    // note on kSwitchId.
    kBypassId = 100,

    // The footswitch. Every pedal's first parameter, and the one thing MIDI learn binds to.
    //
    // TWO DIFFERENT SWITCHES, deliberately. kBypassId is what a host's own bypass button drives
    // and what a host expects to be able to take over (kIsBypass); it takes the plug-in out of
    // circuit whole. kSwitchId is the stompbox's own footswitch: it cross-fades over
    // pedals::kEngageMs and, once it has landed on zero, RESETS the effect's delay line and LFO —
    // which is what a true-bypass pedal does and is why a chorus does not resume mid-sweep. They
    // are not the same event and collapsing them would lose one behaviour or the other.
    kSwitchId = 300,

    // The knobs and the small controls, in table order, from here up. 301, 302, ...
    kKnobIdFirst = 301,

    // MIDI. Control Change reaches a VST3 plug-in only as a parameter change routed by
    // IMidiMapping::getMidiControllerAssignment, so there is one parameter per controller number;
    // Program Change arrives through a ProgramList instead, whose id doubles as the parameter's
    // (EditControllerEx1::ProgramList builds it that way). Both are declared by every plug-in
    // because the footswitch can be learned to either. See common/midilearn.h.
    kMidiCcIdFirst = 1000,
    kMidiProgramListId = 1000 + 128,
};

inline constexpr int kMidiCcCount = 128;
inline constexpr int kMidiProgramCount = 128;

// The unit the 129 MIDI parameters live in, so a host groups them away from the four knobs a
// player actually turns. The pedal's own controls stay in the ROOT unit: they are the whole
// plug-in, and burying them one level down would be filing a stompbox under a subheading.
//
// THIS UNIT MUST BE THE FIRST ONE THE CONTROLLER ADDS. That is a measured requirement carried
// over from rations-amp, where a second unit declared ahead of it pushed it from index 0 to index
// 1 and a footswitch that had worked for months stopped being seen at all; moving the one addUnit
// call back — changing no parameter, flag or id — restored it completely. What that means inside
// the host was never established, and is not claimed here. Nothing is gained by any other order.
inline constexpr Steinberg::Vst::UnitID kMidiUnitId = 1;

// How a control behaves, which is all the controller needs to pick a parameter class.
enum class PedalParamKind {
    Toggle, // step count 1; the footswitch and the Delay's Ping-Pong
    Range,  // Vst::RangeParameter over [min, max]
    List,   // Vst::StringListParameter; the strings come from kDelaySyncNames
};

struct PedalParamSpec {
    Steinberg::Vst::ParamID id;
    const char *title;  // host-visible: what an automation lane is called
    const char *legend; // drawn under the knob, in the pedal's own words
    const char *unit;   // nullptr = none
    PedalParamKind kind;
    double min, max, def;
    int precision;
};

// A pedal's table, passed around as one value. Not std::span: this has to be usable in constexpr
// context under C++17, which is the SDK's minimum and therefore this project's.
struct ParamList {
    const PedalParamSpec *items;
    int count;

    constexpr const PedalParamSpec &operator[](int i) const
    {
        return items[i];
    }
};
template <std::size_t N> constexpr ParamList makeParamList(const PedalParamSpec (&a)[N])
{
    return ParamList{a, static_cast<int>(N)};
}

//--------------------------------------------------------------------------------------------
// Delay sync divisions. "Free" is a VALUE here rather than a separate toggle, so a host cannot
// automate the delay into being synced and free at the same time.
inline constexpr int kDelaySyncCount = 12;
inline constexpr const char *kDelaySyncNames[kDelaySyncCount] = {
    "Free", "1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T",
};
// Beats per repeat for each of the above, index 0 unused (Free reads the Time knob instead).
inline constexpr double kDelaySyncBeats[kDelaySyncCount] = {
    0.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0,
};

//--------------------------------------------------------------------------------------------
// The five tables. Ranges, defaults and precisions are rations-amp's, unchanged, so a pedal
// sounds here exactly as it does on that board.

// Boost — the TS-9. 0..10 rather than 0..1 or a percentage because that is what is printed around
// the knob on the pedal being modelled; the mapping to the circuit (Drive to the clipping stage's
// feedback resistance, Tone to the second-order low-pass) happens in the DSP.
inline constexpr PedalParamSpec kBoostParamArray[] = {
    {kSwitchId, "Footswitch", "", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
    {kKnobIdFirst + 0, "Drive", "Drive", nullptr, PedalParamKind::Range, 0, 10, 5, 1},
    {kKnobIdFirst + 1, "Tone", "Tone", nullptr, PedalParamKind::Range, 0, 10, 5, 1},
    {kKnobIdFirst + 2, "Level", "Level", nullptr, PedalParamKind::Range, 0, 10, 5, 1},
};

inline constexpr PedalParamSpec kChorusParamArray[] = {
    {kSwitchId, "Footswitch", "", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
    {kKnobIdFirst + 0, "Rate", "Rate", "Hz", PedalParamKind::Range, 0.1, 10.0, 0.8, 2},
    {kKnobIdFirst + 1, "Depth", "Depth", "%", PedalParamKind::Range, 0, 100, 50, 0},
    {kKnobIdFirst + 2, "Mix", "Mix", "%", PedalParamKind::Range, 0, 100, 50, 0},
};

// Flanger. Regen is SIGNED: negative feedback gives the hollow "jet", positive gives peaks, and
// they are different enough that a player wants both from one control rather than a polarity
// switch. 95 rather than 100 because unity feedback in a comb filter does not decay.
inline constexpr PedalParamSpec kFlangerParamArray[] = {
    {kSwitchId, "Footswitch", "", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
    {kKnobIdFirst + 0, "Rate", "Rate", "Hz", PedalParamKind::Range, 0.05, 5.0, 0.3, 2},
    {kKnobIdFirst + 1, "Depth", "Depth", "%", PedalParamKind::Range, 0, 100, 70, 0},
    {kKnobIdFirst + 2, "Manual", "Manual", "%", PedalParamKind::Range, 0, 100, 30, 0},
    {kKnobIdFirst + 3, "Regen", "Regen", "%", PedalParamKind::Range, -95, 95, 50, 0},
};

// Delay. Time is in ms and is IGNORED while Sync names a division; the knob stays live and keeps
// its value so unsyncing returns to where the player left it.
//
// "Repeats" rather than "Feedback" on the enclosure: the word is the one an outer knob's legend
// has room for (68 units against 62 at the label size, caught by panelrender's text audit), and it
// is what the object being modelled prints — Boss abbreviates to F.BACK, MXR prints REGEN, and
// "Repeats" says the same thing without an abbreviation. The HOST still sees "Feedback", which is
// the name that has to be unambiguous in an automation lane.
inline constexpr PedalParamSpec kDelayParamArray[] = {
    {kSwitchId, "Footswitch", "", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
    {kKnobIdFirst + 0, "Time", "Time", "ms", PedalParamKind::Range, 20, 2000, 400, 0},
    {kKnobIdFirst + 1, "Feedback", "Repeats", "%", PedalParamKind::Range, 0, 95, 35, 0},
    {kKnobIdFirst + 2, "Tone", "Tone", nullptr, PedalParamKind::Range, 0, 10, 5, 1},
    {kKnobIdFirst + 3, "Mix", "Mix", "%", PedalParamKind::Range, 0, 100, 30, 0},
    {kKnobIdFirst + 4, "Sync", "Sync", nullptr, PedalParamKind::List, 0, kDelaySyncCount - 1, 0, 0},
    {kKnobIdFirst + 5, "Ping-Pong", "Ping", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
};

inline constexpr PedalParamSpec kReverbParamArray[] = {
    {kSwitchId, "Footswitch", "", nullptr, PedalParamKind::Toggle, 0, 1, 0, 0},
    {kKnobIdFirst + 0, "Decay", "Decay", nullptr, PedalParamKind::Range, 0, 10, 4, 1},
    {kKnobIdFirst + 1, "Tone", "Tone", nullptr, PedalParamKind::Range, 0, 10, 5, 1},
    {kKnobIdFirst + 2, "Pre-delay", "Pre", "ms", PedalParamKind::Range, 0, 200, 20, 0},
    {kKnobIdFirst + 3, "Mix", "Mix", "%", PedalParamKind::Range, 0, 100, 25, 0},
};

inline constexpr ParamList kBoostParams = makeParamList(kBoostParamArray);
inline constexpr ParamList kChorusParams = makeParamList(kChorusParamArray);
inline constexpr ParamList kFlangerParams = makeParamList(kFlangerParamArray);
inline constexpr ParamList kDelayParams = makeParamList(kDelayParamArray);
inline constexpr ParamList kReverbParams = makeParamList(kReverbParamArray);

//--------------------------------------------------------------------------------------------
// The largest parameter count this build will accept out of a state blob. A blob is untrusted
// input, so its length prefix is bounded before it is believed: generous enough that a future
// build with far more controls still loads what this one understands, small enough that a corrupt
// length cannot make the reader spin.
inline constexpr Steinberg::int32 kParamStateMax = 64;

//--------------------------------------------------------------------------------------------
// Reading a table.

// Index of a parameter in a table, or -1. Linear over at most seven entries.
inline constexpr int paramIndex(const ParamList &list, Steinberg::Vst::ParamID id)
{
    for (int i = 0; i < list.count; ++i)
        if (list[i].id == id)
            return i;
    return -1;
}

// Every table must START with the footswitch, which is what lets the processor read the engage
// state without a special case and the editor draw the switch from one loop. Checked per table at
// the bottom of this file.
inline constexpr bool startsWithSwitch(const ParamList &list)
{
    return list.count > 0 && list[0].id == kSwitchId && list[0].kind == PedalParamKind::Toggle;
}

// Ids must ascend and be unique, which is what makes "appended, never renumbered" checkable
// rather than merely stated.
inline constexpr bool idsAscend(const ParamList &list)
{
    for (int i = 1; i < list.count; ++i)
        if (list[i].id <= list[i - 1].id)
            return false;
    return true;
}

// --- how a table becomes a face -----------------------------------------------------------
// The enclosure art is BLANK — no knobs, no lettering — so the editor generates each face from
// the table rather than from a per-pedal layout. The rule is one line long: a Range control is a
// knob, and anything else that is not the footswitch is a small text control beside the lamp.
// Both are constexpr and are used at compile time by pedalgeometry.h's static_asserts, which is
// the only thing standing between "a sixth knob was added" and a face that silently draws four.

// The k-th knob (Range control) of a pedal, as an index into its table, or -1. The footswitch is
// skipped by starting at 1; startsWithSwitch() is what makes that safe.
inline constexpr int knobParam(const ParamList &list, int k)
{
    int n = 0;
    for (int i = 1; i < list.count; ++i) {
        if (list[i].kind != PedalParamKind::Range)
            continue;
        if (n++ == k)
            return i;
    }
    return -1;
}
inline constexpr int knobCount(const ParamList &list)
{
    int n = 0;
    while (knobParam(list, n) >= 0)
        ++n;
    return n;
}

// Everything else in the table: today that is the Delay's Sync division and its Ping-Pong switch,
// and nothing on any other pedal. They are drawn as small text controls beside the lamp because a
// list and a two-state switch are both things a knob reads badly.
inline constexpr int miniParam(const ParamList &list, int k)
{
    int n = 0;
    for (int i = 1; i < list.count; ++i) {
        if (list[i].kind == PedalParamKind::Range)
            continue;
        if (n++ == k)
            return i;
    }
    return -1;
}
inline constexpr int miniCount(const ParamList &list)
{
    int n = 0;
    while (miniParam(list, n) >= 0)
        ++n;
    return n;
}

// Normalized (what the host and the parameter queue carry) to plain (what the DSP wants). One
// function, so the controller's RangeParameter and this can never disagree about a range.
inline double pedalPlain(const PedalParamSpec &spec, double norm)
{
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    if (spec.kind == PedalParamKind::List)
        return std::floor(norm * (spec.max - spec.min) + 0.5) + spec.min;
    return spec.min + norm * (spec.max - spec.min);
}
inline double pedalNorm(const PedalParamSpec &spec, double plain)
{
    const double span = spec.max - spec.min;
    if (span <= 0.0)
        return 0.0;
    const double n = (plain - spec.min) / span;
    return n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
}

//--------------------------------------------------------------------------------------------
// What the five tables promise, asserted rather than trusted.
#define RATIONS_CHECK_TABLE(list, name)                                                            \
    static_assert(startsWithSwitch(list), name " must start with its footswitch");                 \
    static_assert(idsAscend(list), name " ids must ascend and be unique");                         \
    static_assert(list.count <= kParamStateMax, name " is longer than a state blob may be");       \
    static_assert(knobCount(list) >= 3, name " has fewer knobs than any enclosure face carries")

RATIONS_CHECK_TABLE(kBoostParams, "Boost");
RATIONS_CHECK_TABLE(kChorusParams, "Chorus");
RATIONS_CHECK_TABLE(kFlangerParams, "Flanger");
RATIONS_CHECK_TABLE(kDelayParams, "Delay");
RATIONS_CHECK_TABLE(kReverbParams, "Reverb");
#undef RATIONS_CHECK_TABLE

// The Delay is the only pedal with anything beside the lamp, and the face layout leaves room for
// exactly two. A third would need somewhere to go before it needed a value.
static_assert(miniCount(kBoostParams) == 0 && miniCount(kChorusParams) == 0 &&
                  miniCount(kFlangerParams) == 0 && miniCount(kReverbParams) == 0,
              "only the Delay carries mini controls");
static_assert(miniCount(kDelayParams) == 2, "the face has room for exactly two mini controls");

//--------------------------------------------------------------------------------------------
// IConnectionPoint messages. MIDI learn is the only thing here that is not a parameter, and it is
// not one for a good reason: a learned binding is a message type, a channel and a data byte, which
// is not a number on a dial and would be nonsense in an automation lane.
//
//   kMsgMidiLearn   controller -> processor, int "row": arm that row, or -1 to disarm.
//   kMsgMidiClear   controller -> processor, int "row": forget that row's binding.
//   kMsgRequestMidi controller -> processor: send the table now (the editor just opened).
//   kMsgMidiTable   processor -> controller: the packed binding, plus whether a row is armed.
inline constexpr const char *kMsgMidiLearn = "RationsPedalMidiLearn";
inline constexpr const char *kMsgMidiClear = "RationsPedalMidiClear";
inline constexpr const char *kMsgRequestMidi = "RationsPedalRequestMidi";
inline constexpr const char *kMsgMidiTable = "RationsPedalMidiTable";
inline constexpr const char *kMidiRowAttr = "row";
inline constexpr const char *kMidiTableAttr = "table";
inline constexpr const char *kMidiArmedAttr = "armed";

//--------------------------------------------------------------------------------------------
// How long the HOST bypass takes to cross-fade, in milliseconds. Not zero: a host bypass button
// is a mouse click on a control that is often automated, and a hard mix change is a click in
// exactly the way a hard footswitch would be. Shorter than the footswitch's engage ramp
// (pedals::kEngageMs) because bypass does not reset anything and so has nothing to hide.
inline constexpr double kBypassRampMs = 5.0;

// Version of the state blob written by getState and accepted by setState / setComponentState.
// Version 1 is the first, and unlike rations-amp's it is length-prefixed from the start — that
// project needed three format bumps to learn that a fixed count in the middle of a blob makes
// every later field unreadable the moment the count changes.
inline constexpr Steinberg::int32 kStateVersion = 1;

} // namespace Rations
