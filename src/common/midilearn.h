// MidiLearn — the table that maps a footswitch button to something this plug-in does.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// A stompbox is a thing you press with your foot, and it has to keep being that with the editor
// closed and the mouse nowhere near it. So the table lives in the processor and is evaluated on
// the audio thread. This header is the shared vocabulary: the processor matches against it, the
// controller caches a copy for the strip under the enclosure to draw, and both ends of the state
// blob agree on its layout.
//
// ONE ROW. In rations-amp this table had nine — four amp channels and five pedal footswitches —
// and everything below is that file with the table cut down and nothing else changed, because
// nothing else needed to change: the machinery was written generic over ParamID from the start,
// and a single-pedal plug-in is the smallest case of it rather than a different thing.
//
// WHAT A PEDAL CAN SEND, AND HOW MUCH OF IT VST3 HANDS BACK. Verified against the SDK rather
// than assumed, because the three message types do NOT arrive by the same route and they do not
// carry the same information:
//
//   * Control Change reaches a VST3 plug-in only as a PARAMETER CHANGE, routed by
//     IMidiMapping::getMidiControllerAssignment (pluginterfaces/vst/ivsteditcontroller.h). That
//     call returns one ParamID per controller number, so the MIDI channel a CC arrived on is not
//     recoverable: sixteen channels collapse onto one parameter. Recovering it would mean
//     declaring 16 x 128 parameters, which is not a thing to inflict on a host's parameter list
//     for a feature that switches an amp channel. So a learned CC matches on ANY channel, and
//     the editor says so.
//   * Program Change does not come through IMidiMapping at all. Controller numbers stop at
//     kCountCtrlNumber (130) and kCtrlProgramChange is 130 - the same number - because 130 and
//     up are the namespace for kLegacyMIDICCOutEvent, which is an OUTPUT event
//     (pluginterfaces/vst/ivstmidicontrollers.h:104-112). The SDK's own host-side converter
//     routes Program Change to a parameter carrying ParameterInfo::kIsProgramChange, found
//     through IUnitInfo::getUnitByBus for that MIDI channel
//     (public.sdk/source/vst/basewrapper/basewrapper.cpp:794-820, 1203-1223). That is the route
//     taken here, and it has the same consequence as CC: the parameter is per unit, not per
//     channel, so a learned Program Change also matches any channel.
//   * Note On arrives directly, as Event::kNoteOnEvent in ProcessData::inputEvents
//     (pluginterfaces/vst/ivstevents.h:161), and NoteOnEvent DOES carry its channel. So a
//     learned note is the one binding of the three that can be pinned to one MIDI channel, and
//     it is stored that way rather than being flattened to match the other two.
//
// WHAT THE ROW PERFORMS. It TOGGLES, and it has to: a footswitch that could only ever turn a
// pedal ON would need a second button to turn it off, and a four-button controller driving five
// pedals does not have eight buttons to spare. The host's own bypass (kBypassId) is deliberately
// NOT on the MIDI path — it is the host's control, automated from the host's lane, and a foot
// that could reach both would have two switches doing nearly the same thing with no way to see
// which one was down.
//
// WHAT COUNTS AS A PRESS depends on the controller, and there are three kinds. This mattered more
// than it looks, because the rule here was originally written for one of them and quietly broke
// the other:
//
//   * PROGRAMMED - each slot sends one fixed number on every press and nothing on release
//     (CC 4 value 127, say, or Program Change 4). This is what a programmable MIDI footswitch
//     normally is, and the identical message arrives every time.
//   * MOMENTARY - 127 when the foot goes down, 0 when it comes up. A spring-return switch.
//   * ALTERNATING - 127, then 0, then 127, one message per press, the value tracking a latch
//     inside the controller.
//
// A press is therefore any value at or above 64 - the MIDI switch threshold - and a value below it
// is a release and does nothing. The rule used to require a RISING edge, at or above 64 having
// previously been below, which serves a momentary switch exactly and makes a PROGRAMMED one work
// once and then go dead: its second press is not an edge. That was invisible on a channel row,
// because selecting Clean twice is selecting Clean, and it would have been fatal on a pedal row.
// Measured against the built bundle rather than reasoned about: three presses of one value gave
// on, nothing, nothing.
//
// What the edge test was really protecting is done by the BLOCK instead, in the processor: the
// thing that must not fire repeatedly is a host writing the same value into the parameter every
// block, and that is exactly a repeat in the immediately following block. A foot cannot arrive
// twice inside one 2.67 ms period, so no real press is suppressed and no clock is consulted.
//
// ALTERNATING is the one kind not fully served: its releases are indistinguishable from a
// momentary switch's, so it takes two stamps per change. Serving it instead would mean following
// the value, which would make a momentary switch useless - on only while a foot was held down -
// so it is a mode to avoid programming rather than a case to guess at.

#pragma once

#include "pedalids.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <cstdint>
#include <string>

namespace Rations
{

// What kind of MIDI message a row is listening for. Values are persisted in the state blob, so
// they are fixed once written: append, never renumber.
// Unlearned, not None: <X11/Xlib.h> is in this editor's include graph and defines None as a
// macro, so a member by that name does not survive the preprocessor on the platform this is
// built on.
enum class MidiMsg : std::uint32_t {
    Unlearned = 0, // the row is not learned
    ControlChange = 1,
    ProgramChange = 2,
    NoteOn = 3,
};

// Channel 0 .. 15, or this. CC and Program Change are always kAnyChannel for the reasons in the
// file header; a note may be either.
inline constexpr int kMidiAnyChannel = -1;

// One learned binding. Small and trivially copyable on purpose: it is packed into a single
// atomic word so the audio thread can read a row without a lock and without ever seeing half of
// an edit.
struct MidiBinding {
    MidiMsg msg = MidiMsg::Unlearned;
    int channel = kMidiAnyChannel; // 0 .. 15, or kMidiAnyChannel
    int data1 = 0;                 // controller number, program number, or note number

    bool learned() const
    {
        return msg != MidiMsg::Unlearned;
    }
    bool operator==(const MidiBinding &o) const
    {
        return msg == o.msg && channel == o.channel && data1 == o.data1;
    }
};

// Pack a binding into one 32-bit word, and back. Two bits of type, five of channel (0 = any,
// 1 .. 16 = channel + 1) and seven of data, so the whole thing is 14 bits and an atomic<uint32>
// is lock-free on every platform this builds for. unpack() clamps rather than trusting its
// input, because the same words come back out of an untrusted state blob.
std::uint32_t packBinding(const MidiBinding &b);
MidiBinding unpackBinding(std::uint32_t word);

// What a row does with its parameter. See the file header for why a channel sets and a pedal
// toggles, and for what a latching footswitch costs.
enum class MidiAction {
    Set = 0,    // store the row's value
    Toggle = 1, // flip between 0 and 1 - only legal on a parameter whose step count is 1
};

// What a row performs when its binding matches: a parameter, an action, and the value the action
// uses. Fixed at compile time, so the audio thread never has to publish a target, only a binding.
struct MidiLearnTarget {
    const char *label;             // what the settings page calls this row
    Steinberg::Vst::ParamID param; // what it performs
    MidiAction action;             // ... and how
    double value;                  // what Set stores, normalized. Toggle does not read it.
};

// One row: the footswitch. Written as a table of one rather than as a bare ParamID because the
// matching, packing and description code below is generic over the row, and collapsing it into a
// special case would buy nothing and cost the ability to add a second row later.
inline constexpr int kMidiLearnRowCount = 1;
inline constexpr MidiLearnTarget kMidiLearnRows[kMidiLearnRowCount] = {
    {"Footswitch", kSwitchId, MidiAction::Toggle, 0.0},
};

// Toggle flips between 0 and 1, which is only a value a parameter can take if its step count is
// 1. Every pedal's table starts with a Toggle footswitch — asserted in pedalids.h — and this is
// the other half of the claim: that the row performing it is a Toggle too.
static_assert(kMidiLearnRows[0].param == kSwitchId, "the row must target the footswitch");
static_assert(kMidiLearnRows[0].action == MidiAction::Toggle, "the footswitch row must toggle");

// Does an incoming message match this binding? `channel` is the channel the message arrived on,
// or kMidiAnyChannel when the route did not carry one (CC and Program Change - see the header).
bool bindingMatches(const MidiBinding &b, MidiMsg msg, int channel, int data1);

// One line for the strip under the enclosure: "CC 64", "PC 3", "Note C3 ch 2", or "not learned".
// Never allocates beyond the returned string, and never runs on the audio thread.
std::string describeBinding(const MidiBinding &b);

} // namespace Rations
