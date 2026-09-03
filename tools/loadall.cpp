// loadall — load every bundle into ONE process at once, instantiate them all, and run audio
// through each. Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// WHY THIS EXISTS. These are five sibling plug-ins that a user will load into one host process at
// the same time, and that is not five times one problem. Two bundles sharing a symbol, a static,
// or a resource path is a crash or a silent cross-talk that neither bundle can see in isolation,
// and it is invisible in every test that loads one plug-in at a time — including the SDK
// validator, which is handed one bundle and never sees the others. On Linux the sharp edge is
// STB_GNU_UNIQUE: such a symbol is bound process-wide across every dlopen'ed library that defines
// it, so five plug-ins that each believe they own a static would be sharing one. The export gate
// in scripts/makedist-linux.sh proves no bundle exports such a symbol; this proves the five
// actually coexist.
//
// It is not a DAW, and it does not replace loading them in one — that is a release gate a person
// runs. It is the part of that gate a machine can run on every build, on both platforms, with no
// display and no audio device.
//
// WHAT IT ASSERTS, in the order the failures matter:
//
//   * all the bundles load, and stay loaded, at the same time. No module is released until every
//     one of them has been instantiated, processed and torn down.
//   * each factory offers a distinct processor class ID. Two bundles claiming one UID is a
//     copy-paste mistake in ids.h that a host answers by silently loading whichever it saw first.
//   * each plug-in negotiates a bus arrangement, activates, and processes a block into finite,
//     non-silent output — with all five live, so a static one of them scribbled on is scribbled on
//     before the last one runs.
//   * each editor view is created and released unattached, which is what a host's plug-in scan
//     does and what the validator does.
//
// Usage: loadall <bundle> [<bundle> ...]

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Steinberg;

namespace
{

constexpr int kBlock = 128;
// Long enough that the host-bypass ramp every processor comes up in has finished, so what is
// measured at the end is the pedal rather than the fade. What the pedals SOUND like is
// tools/pedalcheck's question, not this one; this asks whether five of them can run at once.
constexpr int kBlocks = 64;
constexpr double kRate = 48000.0;

int gFailures = 0;

void fail(const char *what, const char *detail)
{
    printf("    FAIL  %s: %s\n", what, detail);
    ++gFailures;
}

// One loaded bundle and everything made from it. Held in a vector for the whole run: the point of
// the tool is that none of this is released until all of it has been exercised.
struct Loaded {
    std::string path;
    std::string name;
    VST3::Hosting::Module::Ptr module;
    IPtr<Vst::PlugProvider> provider;
    Vst::IComponent *component = nullptr;
    Vst::IEditController *controller = nullptr;
    FUnknownPtr<Vst::IAudioProcessor> processor;
    Vst::HostProcessData data;
    std::vector<float> in, outL, outR;
    int outChannels = 0;
    FUID uid;
    Vst::ParameterChanges switchOn;
};

// The footswitch, found the way a host finds it: by asking the controller what its parameters are
// called. loadall could include this project's own pedalids.h and read kSwitchId out of it, and
// then it would not be testing that a host can find the control at all.
Steinberg::Vst::ParamID findFootswitch(Vst::IEditController *controller, bool &found)
{
    found = false;
    const Steinberg::int32 count = controller->getParameterCount();
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        if (controller->getParameterInfo(i, info) != kResultOk)
            continue;
        char title[128] = {};
        // ParameterInfo::title is a Vst::String128, which is 128 char16 by definition.
        Steinberg::UString(info.title, 128).toAscii(title, sizeof(title));
        if (strcmp(title, "Footswitch") == 0) {
            found = true;
            return info.id;
        }
    }
    return 0;
}

// Mono in, stereo out, then read back what the plug-in agreed to rather than trusting the request
// — the Boost declares mono/mono and refuses a stereo input, which is a real answer and not an
// error. This mirrors what standalone/main.cpp negotiates, deliberately: a difference between the
// two would mean this tool is not testing what a user runs.
bool negotiate(Loaded &p)
{
    Vst::SpeakerArrangement wantIn = Vst::SpeakerArr::kMono;
    Vst::SpeakerArrangement wantOut = Vst::SpeakerArr::kStereo;
    if (p.processor->setBusArrangements(&wantIn, 1, &wantOut, 1) != kResultTrue) {
        wantOut = Vst::SpeakerArr::kMono;
        if (p.processor->setBusArrangements(&wantIn, 1, &wantOut, 1) != kResultTrue) {
            fail(p.name.c_str(), "refused both mono->stereo and mono->mono");
            return false;
        }
    }
    Vst::SpeakerArrangement gotIn = 0, gotOut = 0;
    if (p.processor->getBusArrangement(Vst::kInput, 0, gotIn) != kResultTrue ||
        p.processor->getBusArrangement(Vst::kOutput, 0, gotOut) != kResultTrue) {
        fail(p.name.c_str(), "would not say what arrangement it settled on");
        return false;
    }
    if (Vst::SpeakerArr::getChannelCount(gotIn) != 1) {
        fail(p.name.c_str(), "settled on something other than a mono input");
        return false;
    }
    p.outChannels = Vst::SpeakerArr::getChannelCount(gotOut);
    if (p.outChannels < 1 || p.outChannels > 2) {
        fail(p.name.c_str(), "settled on an output bus that is neither mono nor stereo");
        return false;
    }
    return true;
}

bool activate(Loaded &p)
{
    Vst::ProcessSetup setup{};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kRate;
    if (p.processor->setupProcessing(setup) != kResultOk) {
        fail(p.name.c_str(), "setupProcessing failed");
        return false;
    }
    if (p.component->setActive(true) != kResultOk) {
        fail(p.name.c_str(), "setActive(true) failed");
        return false;
    }
    // Called because a host calls it, and its result deliberately not treated as a failure:
    // these plug-ins do not implement it, so the SDK's base class answers kNotImplemented, which
    // is legal — nothing here depends on the transition, and the denormal control that might have
    // is re-armed inside process() instead.
    (void)p.processor->setProcessing(true);

    // bufferSamples 0: prepare() builds the bus structures but does not own the sample memory, so
    // the pointers below are ours and stay valid for the whole run.
    if (!p.data.prepare(*p.component, 0, Vst::kSample32)) {
        fail(p.name.c_str(), "could not prepare the process data");
        return false;
    }
    p.in.assign(kBlock, 0.0f);
    p.outL.assign(kBlock, 0.0f);
    p.outR.assign(kBlock, 0.0f);
    for (int i = 0; i < kBlock; ++i)
        p.in[static_cast<size_t>(i)] =
            0.4f * std::sin(2.0f * 3.14159265358979f * 220.0f * float(i) / float(kRate));

    p.data.numSamples = kBlock;
    p.data.symbolicSampleSize = Vst::kSample32;
    p.data.processMode = Vst::kRealtime;

    // Through setChannelBuffer, never by assigning channelBuffers32. prepare() allocated that
    // ARRAY OF POINTERS even though it allocated no samples, and its destructor frees it, so
    // pointing it at an array of our own is a free() of memory we did not allocate — which is
    // what the first version of this did, and it aborted inside the teardown a long way from the
    // cause.
    // STOMP IT ON. A pedal comes up switched off — a real one does too, and the parent project's
    // pedalboard does the same — and Pedal::process() skips a pedal that is out of circuit, so a
    // run that left the switch alone would exercise almost none of the DSP. The value is delivered
    // through the host's own input parameter queue, which is the only route a plug-in has to
    // accept.
    bool found = false;
    const Vst::ParamID switchId = findFootswitch(p.controller, found);
    if (!found) {
        fail(p.name.c_str(), "has no parameter called Footswitch");
        return false;
    }
    Steinberg::int32 queueIndex = 0;
    if (Vst::IParamValueQueue *queue = p.switchOn.addParameterData(switchId, queueIndex)) {
        Steinberg::int32 pointIndex = 0;
        queue->addPoint(0, 1.0, pointIndex);
    } else {
        fail(p.name.c_str(), "could not queue the footswitch");
        return false;
    }
    p.data.inputParameterChanges = &p.switchOn;

    if (!p.data.setChannelBuffer(Vst::kInput, 0, 0, p.in.data())) {
        fail(p.name.c_str(), "could not attach the input buffer");
        return false;
    }
    for (int ch = 0; ch < p.outChannels; ++ch) {
        float *buf = ch == 0 ? p.outL.data() : p.outR.data();
        if (!p.data.setChannelBuffer(Vst::kOutput, 0, ch, buf)) {
            fail(p.name.c_str(), "could not attach an output buffer");
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: loadall <bundle> [<bundle> ...]\n"
                        "  Loads every bundle into one process at once and runs audio through\n"
                        "  each of them while all of them are live.\n");
        return 2;
    }

    // The host context must be published before anything is instantiated: ComponentBase::
    // allocateMessage() asks it for IMessage instances, and without one the controller-to-
    // processor messages these plug-ins use for MIDI learn are silently dropped.
    Vst::HostApplication hostContext;
    Vst::PluginContextFactory::instance().setPluginContext(&hostContext);

    std::vector<std::unique_ptr<Loaded>> loaded;

    // --- load them all, and keep them all loaded --------------------------------------------
    printf("loadall: %d bundle(s), one process\n", argc - 1);
    for (int i = 1; i < argc; ++i) {
        auto p = std::make_unique<Loaded>();
        p->path = argv[i];

        std::string error;
        p->module = VST3::Hosting::Module::create(p->path, error);
        if (!p->module) {
            printf("    FAIL  %s did not load: %s\n", p->path.c_str(), error.c_str());
            ++gFailures;
            continue;
        }

        auto factory = p->module->getFactory();
        for (auto &classInfo : factory.classInfos()) {
            if (classInfo.category() != kVstAudioEffectClass)
                continue;
            p->name = classInfo.name();
            p->uid = FUID::fromTUID(classInfo.ID().data());
            p->provider = owned(new Vst::PlugProvider(factory, classInfo, true));
            if (p->provider->initialize())
                break;
            p->provider = nullptr;
        }
        if (!p->provider) {
            printf("    FAIL  %s has no audio effect class that would initialize\n",
                   p->path.c_str());
            ++gFailures;
            continue;
        }

        p->component = p->provider->getComponent();
        p->controller = p->provider->getController();
        p->processor = FUnknownPtr<Vst::IAudioProcessor>(p->component);
        if (!p->component || !p->controller || !p->processor) {
            printf("    FAIL  %s did not provide a component, a controller and a processor\n",
                   p->name.c_str());
            ++gFailures;
            continue;
        }
        printf("    ok    loaded %s\n", p->name.c_str());
        loaded.push_back(std::move(p));
    }

    // --- distinct identities ----------------------------------------------------------------
    for (size_t a = 0; a < loaded.size(); ++a) {
        for (size_t b = a + 1; b < loaded.size(); ++b) {
            if (loaded[a]->uid == loaded[b]->uid) {
                char detail[256];
                snprintf(detail, sizeof(detail), "shares its processor UID with %s",
                         loaded[b]->name.c_str());
                fail(loaded[a]->name.c_str(), detail);
            }
        }
    }

    // --- all of them live at once, then all of them processing ------------------------------
    for (auto &p : loaded) {
        if (!negotiate(*p))
            continue;
        if (!activate(*p))
            continue;
    }

    // INTERLEAVED, a block at a time, rather than one plug-in from start to finish. If two
    // bundles shared a static this is the arrangement that exposes it: each one's state has to
    // survive the other four running between its own blocks, which is exactly what a host does
    // with five pedals on one track.
    bool processFailed = false;
    for (int block = 0; block < kBlocks && !processFailed; ++block) {
        for (auto &p : loaded) {
            if (p->outChannels == 0 || !p->data.outputs)
                continue;
            if (p->processor->process(p->data) != kResultOk) {
                fail(p->name.c_str(), "process() failed");
                processFailed = true;
                break;
            }
        }
    }

    for (auto &p : loaded) {
        if (p->outChannels == 0 || !p->data.outputs)
            continue;

        double peak = 0.0;
        bool finite = true;
        for (int ch = 0; ch < p->outChannels; ++ch) {
            const float *v = ch == 0 ? p->outL.data() : p->outR.data();
            for (int i = 0; i < kBlock; ++i) {
                if (!std::isfinite(v[i]))
                    finite = false;
                peak = std::max(peak, std::fabs(double(v[i])));
            }
        }
        char detail[160];
        snprintf(detail, sizeof(detail), "%d blocks, %d channel(s) out, peak %.4f", kBlocks,
                 p->outChannels, peak);
        if (!finite)
            fail(p->name.c_str(), "wrote a NaN or an infinity");
        else if (peak <= 0.0)
            fail(p->name.c_str(), "wrote silence from a signal that was not silent");
        else
            printf("    ok    %-16s %s\n", p->name.c_str(), detail);

        // A host's plug-in scan creates and releases the editor without ever attaching it, and so
        // does the validator. Doing it here, with five plug-ins live, is the cheapest place for a
        // view constructor that touched shared state to show itself.
        if (IPlugView *view = p->controller->createView(Vst::ViewType::kEditor)) {
            view->release();
            printf("    ok    %-16s created and released its editor unattached\n", p->name.c_str());
        } else {
            fail(p->name.c_str(), "offered no editor view");
        }
    }

    // --- tear down, still all together ------------------------------------------------------
    for (auto &p : loaded) {
        if (p->processor)
            p->processor->setProcessing(false);
        if (p->component)
            p->component->setActive(false);
        p->data.unprepare();
        p->processor = nullptr;
        p->provider = nullptr; // releases the component and the controller
    }
    loaded.clear(); // and only now are the modules unloaded

    printf("loadall: %d failure(s)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
