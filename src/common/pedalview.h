// PedalView — the editor: one enclosure, and the strip that says what works its footswitch.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// Derives from NativePlugView, which is X11PlugView on Linux and Win32PlugView on Windows — the
// one seam in this project that knows about either. Everything this class does is expressed
// through that base's hooks (onDraw, onMouse*, onTick, constrainSize), so nothing below includes
// a platform header and the two platforms cannot drift apart in behaviour.
//
// WHAT IT DOES NOT DO IS DRAW. The face painter is src/common/pedalface.cpp, which knows nothing
// about VST3 or windowing — see the note at the top of pedalface.h for why that split is load
// bearing. This class owns the window, the parameter mirror, the pointer and the caches.
//
// THREADING: every method here runs on the host's UI/run-loop thread, which is also the thread
// the controller calls ParamChanged from. Nothing is shared with the audio thread, so nothing is
// locked or atomic.
#pragma once

#include "pedalcontroller.h"
#include "pedalface.h"
#include "pedalgeometry.h"
#include "pedalids.h"

#include "gfx/canvas.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "platform/plugview.h"
#include "platform/respath.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Rations
{

template <typename Traits> class PedalView : public Steinberg::NativePlugView
{
public:
    using Controller = PedalController<Traits>;
    static constexpr ParamList kParams = Traits::kParams;
    static constexpr int kParamCount = kParams.count;

    explicit PedalView(Controller *controller)
        : Steinberg::NativePlugView(static_cast<Steinberg::Vst::EditController *>(controller)),
          mController(controller)
    {
        Steinberg::ViewRect size(0, 0, geo::kWindowW, geo::kWindowH);
        setRect(size);
        for (int i = 0; i < kParamCount; ++i)
            mNorm[i] = pedalNorm(kParams[i], kParams[i].def);
    }

    // Every route into a parameter passes through the controller's setParamNormalized, which
    // forwards here: automation, a generic UI, a state load, and the plug-in's own MIDI-driven
    // footswitch all arrive by this one door.
    void ParamChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value)
    {
        if (id == kBypassId) {
            mBypassNorm = value;
            invalidate();
            return;
        }
        const int index = paramIndex(kParams, id);
        if (index < 0)
            return; // a MIDI CC parameter; nothing on the face shows one
        mNorm[index] = value;
        invalidate();
    }

    // The processor's MIDI table arrived, or the armed row changed.
    void MidiTableChanged()
    {
        invalidate();
    }

protected:
    //---from NativePlugView--------------------------------------------------------------
    void onAttached() SMTG_OVERRIDE
    {
        const std::string res = resourceDir();
        mFonts.load(res);
        mImages.setResourceDir(res);
        // The binding was learned before this view existed, and nothing else would tell the strip
        // what it is.
        if (mController)
            mController->requestMidiTable();
        invalidate();
    }

    void onRemoved() SMTG_OVERRIDE
    {
        endDrag(false);
        mImages.purgeScaled();
    }

    void onDraw(cairo_t *cr) SMTG_OVERRIDE
    {
        // The ground, in DEVICE space, so the letterbox margins a host's odd window size leaves
        // are covered rather than showing whatever was behind us.
        cairo_save(cr);
        cairo_set_source_rgb(cr, ((geo::kStripBg >> 16) & 0xFF) / 255.0,
                             ((geo::kStripBg >> 8) & 0xFF) / 255.0, (geo::kStripBg & 0xFF) / 255.0);
        cairo_paint(cr);
        cairo_restore(cr);

        // From here on everything is in logical units; this is the only transform.
        cairo_save(cr);
        cairo_translate(cr, mOffX, mOffY);
        cairo_scale(cr, mScale, mScale);
        Canvas c(cr, &mFonts, static_cast<float>(geo::kWindowW), static_cast<float>(geo::kWindowH));

        FaceState s = faceState();
        drawPedalFace(c, mImages, s, mScale);
        drawStrip(c, s, mScale);
        cairo_restore(cr);
    }

    void onMouseDown(int x, int y, int button) SMTG_OVERRIDE
    {
        if (button != 1 || !mController)
            return;
        float lx = 0, ly = 0;
        if (!toLogical(x, y, lx, ly))
            return;

        // The strip first: its buttons sit outside the enclosure, so no test below can reach them
        // and testing them first costs nothing.
        if (ly >= geo::kArtH) {
            const bool learned = mController->midiBinding().learned();
            if (learned && stripClearRect().contains(lx, ly)) {
                mController->clearMidiLearn(0);
                invalidate();
                return;
            }
            if (stripLearnRect().contains(lx, ly)) {
                // Clicking the button that is already listening stops it listening. A Learn
                // button with no way back would leave the plug-in waiting for a message the user
                // has decided not to send.
                mController->armMidiLearn(mController->armedMidiRow() == 0 ? -1 : 0);
                invalidate();
            }
            return;
        }

        // The footswitch and the bypass toggle: both act on the press, the way a switch does.
        // Neither is a drag, so neither takes the pointer.
        const float sdx = lx - geo::kSwitchCX, sdy = ly - geo::kSwitchCY;
        if (sdx * sdx + sdy * sdy <= float(geo::kSwitchHitR) * geo::kSwitchHitR) {
            toggleParam(0);
            return;
        }
        if (lx >= geo::kToggleHitX && lx < geo::kToggleHitX + geo::kToggleHitW &&
            ly >= geo::kToggleHitTop && ly < geo::kToggleHitBottom) {
            setParam(kBypassId, mBypassNorm > 0.5 ? 0.0 : 1.0, true);
            return;
        }

        // The grid. A knob starts a drag; a mini control steps on the press.
        const int slot = hitGrid(lx, ly);
        if (slot < 0)
            return;
        const int knobs = knobCount(kParams);
        if (slot < knobs) {
            mDragKnob = slot;
            mDragIndex = knobParam(kParams, slot);
            mDragStartY = ly;
            mDragStartNorm = mNorm[mDragIndex];
            mController->beginEdit(kParams[mDragIndex].id);
            invalidate();
        } else {
            stepMini(slot - knobs, +1);
        }
    }

    void onMouseMove(int x, int y) SMTG_OVERRIDE
    {
        if (mDragKnob < 0 || !mController)
            return;
        float lx = 0, ly = 0;
        toLogical(x, y, lx, ly); // out of the window is still a valid drag position
        // Up is more, which is what every dial in every host does. The travel is a face height
        // rather than a fixed pixel count so the feel does not change with the window's size.
        const double travel = 260.0;
        const double delta = (mDragStartY - ly) / travel;
        const double fine = mFine ? 0.25 : 1.0;
        setNorm(mDragIndex, mDragStartNorm + delta * fine);
    }

    void onMouseUp(int x, int y, int button) SMTG_OVERRIDE
    {
        (void)x;
        (void)y;
        if (button == 1)
            endDrag(true);
    }

    void onMouseLeave() SMTG_OVERRIDE
    {
        // Deliberately NOT ending a drag: a pointer that slips off the window mid-turn is still
        // turning the knob, and every host's own dials behave that way.
    }

    void onMouseWheel(int x, int y, int delta) SMTG_OVERRIDE
    {
        if (!mController || delta == 0)
            return;
        float lx = 0, ly = 0;
        if (!toLogical(x, y, lx, ly))
            return;
        const int slot = hitGrid(lx, ly);
        if (slot < 0)
            return;
        const int knobs = knobCount(kParams);
        if (slot >= knobs) {
            stepMini(slot - knobs, delta > 0 ? +1 : -1);
            return;
        }
        const int index = knobParam(kParams, slot);
        if (index < 0)
            return;
        // One notch is a fortieth of the sweep, or a hundred and sixtieth with a modifier held.
        const double step = (mFine ? 1.0 / 160.0 : 1.0 / 40.0) * (delta > 0 ? 1.0 : -1.0);
        mController->beginEdit(kParams[index].id);
        setNorm(index, mNorm[index] + step);
        mController->endEdit(kParams[index].id);
    }

    bool onKeyDownNative(Steinberg::char16 key, Steinberg::int16 keyCode,
                         Steinberg::int16 modifiers) SMTG_OVERRIDE
    {
        (void)key;
        (void)keyCode;
        // The only key this editor cares about is the fine-adjust modifier, and it is read as a
        // state rather than an event — see mFine.
        mFine = modifiers != 0;
        return false;
    }

    void onTick() SMTG_OVERRIDE
    {
        // The armed row is the one thing that changes without a parameter changing: the processor
        // clears it the moment it learns something, and the strip has to stop saying "listening".
        if (!mController)
            return;
        const bool armed = mController->armedMidiRow() == 0;
        const bool learned = mController->midiBinding().learned();
        if (armed != mWasArmed || learned != mWasLearned) {
            mWasArmed = armed;
            mWasLearned = learned;
            invalidate();
        }
    }

    bool isResizable() const SMTG_OVERRIDE
    {
        return true;
    }

    // One rule: keep the enclosure's aspect, and stay between the two scale bounds. The height
    // follows the width, because a pedal is a tall object and a host dragging a corner is
    // choosing how much of its screen to give it.
    void constrainSize(int &w, int &h) const SMTG_OVERRIDE
    {
        const double byW = double(w) / geo::kWindowW;
        const double byH = double(h) / geo::kWindowH;
        double s = std::min(byW, byH);
        s = std::clamp(s, geo::kMinScale, geo::kMaxScale);
        w = int(std::lround(geo::kWindowW * s));
        h = int(std::lround(geo::kWindowH * s));
    }

    void onResized(int w, int h) SMTG_OVERRIDE
    {
        mDevW = w;
        mDevH = h;
        // Fit and centre. constrainSize normally leaves no remainder, but a host is entitled to
        // ignore it and hand over any size at all, and a letterboxed pedal is a better answer
        // than a stretched one.
        mScale = std::min(double(w) / geo::kWindowW, double(h) / geo::kWindowH);
        mScale = std::clamp(mScale, geo::kMinScale, geo::kMaxScale);
        mOffX = (w - geo::kWindowW * mScale) * 0.5;
        mOffY = (h - geo::kWindowH * mScale) * 0.5;
        // Cached bitmaps are keyed on their pixel size, and every one of them just changed.
        mImages.purgeScaled();
        invalidate();
    }

private:
    FaceState faceState()
    {
        FaceState s;
        s.params = kParams;
        s.norm = mNorm;
        s.name = Traits::kShortName;
        s.art = Traits::kArt;
        s.bypassed = mBypassNorm > 0.5;
        s.draggingKnob = mDragKnob;
        if (mController) {
            const MidiBinding b = mController->midiBinding();
            s.learned = b.learned();
            // Described only when there is a binding to describe. describeBinding() has an answer
            // for the unlearned case too, and the strip deliberately does not draw it.
            mBindingText = s.learned ? describeBinding(b) : std::string();
            s.bindingText = mBindingText.c_str();
            s.armed = mController->armedMidiRow() == 0;
        }
        return s;
    }

    // Device pixels to logical units. False when the point is outside the drawn area, which is
    // the letterbox a host that ignored constrainSize leaves behind.
    bool toLogical(int x, int y, float &lx, float &ly) const
    {
        if (mScale <= 0.0)
            return false;
        lx = float((x - mOffX) / mScale);
        ly = float((y - mOffY) / mScale);
        return lx >= 0 && ly >= 0 && lx < geo::kWindowW && ly < geo::kWindowH;
    }

    // Which control a point is in, in the same order the painter lays them out: knobs first, then
    // mini controls. -1 for none. Shares knobPos and the mini slots with the painter, which is
    // what keeps the hit test and the picture from drifting apart.
    //
    // The knob hit radius is a little larger than the dial, the way the pedalboard's is: the
    // target is a mouse pointer and the four units cost nothing, because nothing else on the face
    // is within them.
    int hitGrid(float lx, float ly) const
    {
        const int knobs = knobCount(kParams);
        const float r = float(geo::kKnobR) + 10.0f;
        for (int k = 0; k < knobs; ++k) {
            const geo::Point pt = geo::knobPos(knobs, k);
            const float dx = lx - float(pt.x), dy = ly - float(pt.y);
            if (dx * dx + dy * dy <= r * r)
                return k;
        }
        for (int m = 0, minis = miniCount(kParams); m < minis && m < geo::kMiniCount; ++m) {
            if (std::fabs(lx - float(geo::kMiniCX[m])) <= geo::kMiniW * 0.5f &&
                std::fabs(ly - float(geo::kMiniCY)) <= geo::kMiniH * 0.5f)
                return knobs + m;
        }
        return -1;
    }

    void setNorm(int index, double norm)
    {
        if (index < 0 || index >= kParamCount || !mController)
            return;
        norm = std::clamp(norm, 0.0, 1.0);
        if (norm == mNorm[index])
            return;
        mNorm[index] = norm;
        // performEdit tells the host; setParamNormalized keeps the controller's own copy in step,
        // and performEdit does NOT do that for us.
        mController->setParamNormalized(kParams[index].id, norm);
        mController->performEdit(kParams[index].id, norm);
        invalidate();
    }

    void setParam(Steinberg::Vst::ParamID id, double norm, bool wrapEdit)
    {
        if (!mController)
            return;
        if (wrapEdit)
            mController->beginEdit(id);
        mController->setParamNormalized(id, norm);
        mController->performEdit(id, norm);
        if (wrapEdit)
            mController->endEdit(id);
        if (id == kBypassId)
            mBypassNorm = norm;
        invalidate();
    }

    void toggleParam(int index)
    {
        if (index < 0 || index >= kParamCount || !mController)
            return;
        const double next = mNorm[index] > 0.5 ? 0.0 : 1.0;
        mNorm[index] = next;
        const Steinberg::Vst::ParamID id = kParams[index].id;
        mController->beginEdit(id);
        mController->setParamNormalized(id, next);
        mController->performEdit(id, next);
        mController->endEdit(id);
        invalidate();
    }

    // Step a mini control one place. A Toggle flips; a List advances and WRAPS, because the only
    // list here is a ring of note divisions and stopping at the end would make the last one hard
    // to leave.
    void stepMini(int mini, int dir)
    {
        const int index = miniParam(kParams, mini);
        if (index < 0 || !mController)
            return;
        const PedalParamSpec &spec = kParams[index];
        if (spec.kind == PedalParamKind::Toggle) {
            toggleParam(index);
            return;
        }
        const int count = int(spec.max - spec.min) + 1;
        if (count <= 1)
            return;
        int step = int(std::lround(pedalPlain(spec, mNorm[index]) - spec.min));
        step = (step + dir % count + count) % count;
        const double norm = pedalNorm(spec, spec.min + step);
        mController->beginEdit(spec.id);
        mNorm[index] = norm;
        mController->setParamNormalized(spec.id, norm);
        mController->performEdit(spec.id, norm);
        mController->endEdit(spec.id);
        invalidate();
    }

    void endDrag(bool commit)
    {
        (void)commit;
        if (mDragKnob < 0)
            return;
        if (mController && mDragIndex >= 0)
            mController->endEdit(kParams[mDragIndex].id);
        mDragKnob = -1;
        mDragIndex = -1;
        invalidate();
    }

    Controller *mController = nullptr;

    FontStack mFonts;
    ImageCache mImages;

    double mNorm[kParamCount] = {};
    double mBypassNorm = 0.0;
    std::string mBindingText; // empty until the footswitch is learned

    double mScale = 1.0, mOffX = 0.0, mOffY = 0.0;
    int mDevW = geo::kWindowW, mDevH = geo::kWindowH;

    int mDragKnob = -1;  // grid slot, for the readout the painter draws
    int mDragIndex = -1; // index into kParams
    float mDragStartY = 0.0f;
    double mDragStartNorm = 0.0;
    bool mFine = false;

    bool mWasArmed = false;
    bool mWasLearned = false;
};

} // namespace Rations
