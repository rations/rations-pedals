// pedalcheck — proves each pedal does what its panel says, offline and without a host.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// TWO KINDS OF CHECK, and they answer different questions.
//
//   BEHAVIOUR. Turning Drive up must produce more harmonic distortion; Mix at zero must pass the
//   dry signal untouched; the Delay's first repeat must arrive when the Time knob says; the
//   Reverb's tail must decay in about the time Decay asks for; a footswitch must engage without a
//   step. These are the claims a player would make about the object, checked against the object.
//
//   THE GOLDEN STREAM. One fixed stimulus, one fixed set of settings, per pedal, hashed. That
//   hash is not arbitrary: it was recorded from a build whose output had just been shown
//   BYTE-IDENTICAL to the same DSP running inside rations-amp, which is where these five pedals
//   come from. It is what stops a well-meaning "simplification" of the interpolator in
//   dsp/sample.h, or of the Chorus's now-stereo loop, from quietly changing what the pedals sound
//   like. A behavioural check would not catch a change of one part in a million; this does.
//
//   If a change to the DSP is DELIBERATE, the hash must be updated in the same commit as the
//   change, and the commit has to say what moved and why. An unexplained hash update is the thing
//   this file exists to make visible.
//
// No VST3 and no host: this links the pedal headers directly.

#include "common/pedalids.h"
#include "pedals/boost.h"
#include "pedals/chorus.h"
#include "pedals/delay.h"
#include "pedals/flanger.h"
#include "pedals/reverb.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Rations;
using namespace Rations::pedals;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;
int gFailures = 0;
int gChecks = 0;

void check(bool ok, const char *what, const char *detail = "")
{
    ++gChecks;
    if (ok) {
        printf("    ok    %s %s\n", what, detail);
        return;
    }
    printf("    FAIL  %s %s\n", what, detail);
    ++gFailures;
}

std::string fmt(const char *f, double a, double b = 0.0, double c = 0.0)
{
    char buf[160];
    snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

//--- the golden stream -------------------------------------------------------------------------
// The stimulus, and it must never change: a swept sine plus a reproducible LCG noise floor, so
// both the smooth and the broadband behaviour of every pedal is exercised. The generator is
// written out rather than pulled from <random>, whose engines are specified but whose
// distributions are not, and a distribution that differed between libstdc++ and libc++ would make
// the hash a property of the standard library rather than of the DSP.
constexpr int kGoldenBlocks = 120; // 15360 samples, 0.32 s

void goldenStimulus(std::vector<double> &l, std::vector<double> &r, int n)
{
    l.resize(static_cast<size_t>(n));
    r.resize(static_cast<size_t>(n));
    unsigned s = 0x1234567u;
    double ph = 0.0;
    for (int i = 0; i < n; ++i) {
        const double f = 80.0 + 3000.0 * double(i) / double(n);
        ph += 2.0 * M_PI * f / kRate;
        s = s * 1664525u + 1013904223u;
        const double noise = (double(s >> 8) / 8388608.0 - 1.0) * 0.02;
        l[size_t(i)] = 0.4 * std::sin(ph) + noise;
        r[size_t(i)] = 0.4 * std::sin(ph * 1.003 + 0.7) - noise;
    }
}

std::uint64_t fnv1a(const void *data, size_t bytes)
{
    const unsigned char *p = static_cast<const unsigned char *>(data);
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// Run one pedal over the golden stimulus and hash what comes out. `stereo` says whether the right
// channel is fed and hashed, and must match how the pedal was hashed in the first place.
template <typename P>
std::uint64_t goldenHash(const double *params, bool stereo, void (*prime)(P &) = nullptr)
{
    P p;
    if (prime)
        prime(p);
    p.prepare(kRate, kBlock);
    p.setEngaged(true);
    std::vector<double> l, r;
    goldenStimulus(l, r, kBlock * kGoldenBlocks);
    for (int b = 0; b < kGoldenBlocks; ++b) {
        p.setParams(params);
        p.process(l.data() + b * kBlock, stereo ? r.data() + b * kBlock : nullptr, kBlock);
    }
    std::uint64_t h = fnv1a(l.data(), l.size() * sizeof(double));
    if (stereo) {
        // Hashed as two separate runs of bytes, in the order the reference dump wrote them.
        const std::uint64_t hr = fnv1a(r.data(), r.size() * sizeof(double));
        (void)hr;
        std::vector<double> both;
        both.reserve(l.size() + r.size());
        both.insert(both.end(), l.begin(), l.end());
        both.insert(both.end(), r.begin(), r.end());
        h = fnv1a(both.data(), both.size() * sizeof(double));
    }
    return h;
}

void checkGolden(const char *name, std::uint64_t got, std::uint64_t want)
{
    char detail[96];
    snprintf(detail, sizeof(detail), "0x%016llX", (unsigned long long)got);
    if (got == want) {
        check(true, "golden stream", detail);
        return;
    }
    char both[160];
    snprintf(both, sizeof(both), "got 0x%016llX, recorded 0x%016llX", (unsigned long long)got,
             (unsigned long long)want);
    check(false, "golden stream", both);
    printf("          %s's DSP has changed. If that was deliberate, update the hash in this file\n"
           "          in the same commit and say what moved; if it was not, something in\n"
           "          dsp/sample.h or in the pedal itself has been altered.\n",
           name);
}

//--- measurement helpers -----------------------------------------------------------------------
double rms(const std::vector<double> &v, size_t from = 0, size_t to = 0)
{
    if (to == 0)
        to = v.size();
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += v[i] * v[i];
    const size_t n = to > from ? to - from : 1;
    return std::sqrt(s / double(n));
}

double peak(const std::vector<double> &v, size_t from = 0, size_t to = 0)
{
    if (to == 0)
        to = v.size();
    double m = 0;
    for (size_t i = from; i < to; ++i)
        m = std::max(m, std::fabs(v[i]));
    return m;
}

void sine(std::vector<double> &v, int n, double hz, double amp)
{
    v.assign(size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        v[size_t(i)] = amp * std::sin(2.0 * M_PI * hz * i / kRate);
}

// Energy at a frequency, by direct correlation. A whole FFT for one bin would be more code and no
// more answer.
double binMag(const std::vector<double> &v, double hz, size_t from, size_t to)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        const double a = 2.0 * M_PI * hz * double(i) / kRate;
        re += v[i] * std::cos(a);
        im += v[i] * std::sin(a);
    }
    const double n = double(to - from);
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

// Run a pedal over a buffer in kBlock-sized pieces, the way a host would.
template <typename P>
void run(P &p, std::vector<double> &l, std::vector<double> *r, const double *params)
{
    const int n = int(l.size());
    for (int i = 0; i < n; i += kBlock) {
        const int m = std::min(kBlock, n - i);
        p.setParams(params);
        p.process(l.data() + i, r ? r->data() + i : nullptr, m);
    }
}

//--- Boost --------------------------------------------------------------------------------------
void checkBoost()
{
    printf("  boost\n");
    // Drive up must mean more harmonic distortion. Measured as the third harmonic relative to the
    // fundamental, which is what a symmetric diode clipper produces most of.
    //
    // AT A LOW INPUT LEVEL, and that is not the test being made easy — it is where the control
    // actually lives. Drive sets the clipping stage's gain, so it decides how hard the signal
    // hits the diodes; once the diodes are conducting hard the pedal is clipping and turning the
    // knob further changes little. Measured on this build, 3rd/1st against Drive:
    //
    //   input 0.05   0.0065 -> 0.1187 -> 0.1634 -> 0.1801 -> 0.1884   (Drive 0, 2.5, 5, 7.5, 10)
    //   input 0.30   0.1248 -> 0.1666 -> 0.1715 -> 0.1732 -> 0.1741
    //
    // At 0.3 the whole knob is worth 39 % more third harmonic and a test there proves almost
    // nothing; at 0.05 it is worth twenty-nine times as much. A quiet signal is also the case a
    // Tube Screamer is for.
    double thd[2] = {0, 0};
    const double drives[2] = {0.0, 10.0};
    for (int k = 0; k < 2; ++k) {
        Boost b;
        b.prepare(kRate, kBlock);
        b.setEngaged(true);
        std::vector<double> l;
        sine(l, 24000, 220.0, 0.05);
        const double p[] = {1.0, drives[k], 5.0, 5.0};
        run(b, l, nullptr, p);
        const size_t from = 8000, to = l.size();
        const double f1 = binMag(l, 220.0, from, to);
        const double f3 = binMag(l, 660.0, from, to);
        thd[k] = f1 > 1e-9 ? f3 / f1 : 0.0;
    }
    check(thd[1] > thd[0] * 5.0, "Drive adds harmonics",
          fmt("(3rd/1st: %.4f at Drive 0 -> %.4f at 10)", thd[0], thd[1]).c_str());

    // Level must be monotonic in output, and must not be the same at both ends.
    double lvl[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        Boost b;
        b.prepare(kRate, kBlock);
        b.setEngaged(true);
        std::vector<double> l;
        sine(l, 16000, 440.0, 0.2);
        const double p[] = {1.0, 5.0, 5.0, k == 0 ? 1.0 : 9.0};
        run(b, l, nullptr, p);
        lvl[k] = rms(l, 6000);
    }
    check(lvl[1] > lvl[0] * 1.5, "Level raises output",
          fmt("(rms %.5f -> %.5f)", lvl[0], lvl[1]).c_str());

    // Tone must change the balance of high to low. Same input, two tone settings.
    double hi[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        Boost b;
        b.prepare(kRate, kBlock);
        b.setEngaged(true);
        std::vector<double> l;
        sine(l, 16000, 3000.0, 0.2);
        const double p[] = {1.0, 5.0, k == 0 ? 0.0 : 10.0, 5.0};
        run(b, l, nullptr, p);
        hi[k] = rms(l, 6000);
    }
    check(std::fabs(hi[1] - hi[0]) > hi[0] * 0.15, "Tone changes the top end",
          fmt("(3 kHz rms %.5f at Tone 0 -> %.5f at 10)", hi[0], hi[1]).c_str());

    check(Boost::kLatencySamples > 0.0, "reports the oversampler's latency",
          fmt("(%.1f samples)", Boost::kLatencySamples).c_str());

    const double golden[] = {1.0, 7.5, 3.0, 6.0};
    checkGolden("Boost", goldenHash<Boost>(golden, false), 0xD8AA5E0C672FB329ull);
}

//--- Chorus -------------------------------------------------------------------------------------
void checkChorus()
{
    printf("  chorus\n");
    // Mix at zero is the dry signal, sample for sample. This is the check that catches a wet path
    // leaking into the output, which is the fault a mix control exists to prevent.
    {
        Chorus c;
        c.prepare(kRate, kBlock);
        c.setEngaged(true);
        std::vector<double> l, dry;
        sine(l, 8000, 440.0, 0.3);
        dry = l;
        const double p[] = {1.0, 2.0, 50.0, 0.0};
        run(c, l, nullptr, p);
        double worst = 0;
        for (size_t i = 0; i < l.size(); ++i)
            worst = std::max(worst, std::fabs(l[i] - dry[i]));
        check(worst < 1e-12, "Mix 0 passes the dry signal",
              fmt("(worst |wet-dry| %.3g)", worst).c_str());
    }

    // The two channels must DIFFER on a stereo chain, and by more than rounding: that is the whole
    // of what kRightPhase buys, and a regression to one shared line would be silent otherwise.
    {
        Chorus c;
        c.prepare(kRate, kBlock);
        c.setEngaged(true);
        std::vector<double> l, r;
        sine(l, 24000, 440.0, 0.3);
        r = l;
        const double p[] = {1.0, 1.5, 80.0, 100.0};
        run(c, l, &r, p);
        double diff = 0;
        for (size_t i = 4000; i < l.size(); ++i)
            diff = std::max(diff, std::fabs(l[i] - r[i]));
        check(diff > 0.01, "the two channels are decorrelated",
              fmt("(max |L-R| %.4f)", diff).c_str());
    }

    // A mono chain must leave the right channel alone — the contract pedals::Pedal states.
    {
        Chorus c;
        c.prepare(kRate, kBlock);
        c.setEngaged(true);
        std::vector<double> l;
        sine(l, 4000, 440.0, 0.3);
        run(c, l, nullptr, (const double[]){1.0, 2.0, 50.0, 50.0});
        check(peak(l) > 0.0, "runs on a mono chain", "");
    }

    const double golden[] = {1.0, 2.3, 65.0, 40.0};
    checkGolden("Chorus", goldenHash<Chorus>(golden, false), 0x91E022DE0A468313ull);
}

//--- Flanger ------------------------------------------------------------------------------------
void checkFlanger()
{
    printf("  flanger\n");
    // Depth zero is a fixed comb, not a sweeping one: the output must be periodic with the input
    // rather than wandering. Checked as the variation of block rms over time.
    {
        Flanger f;
        f.prepare(kRate, kBlock);
        f.setEngaged(true);
        std::vector<double> l, r;
        sine(l, 24000, 700.0, 0.3);
        r = l;
        const double p[] = {1.0, 1.0, 0.0, 50.0, 0.0};
        run(f, l, &r, p);
        double lo = 1e9, hi = 0;
        for (size_t b = 8000; b + 2000 < l.size(); b += 2000) {
            const double v = rms(l, b, b + 2000);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        check(hi < lo * 1.05, "Depth 0 does not sweep",
              fmt("(block rms %.5f..%.5f)", lo, hi).c_str());
    }

    // Depth up must sweep: the same measure must now vary.
    {
        Flanger f;
        f.prepare(kRate, kBlock);
        f.setEngaged(true);
        std::vector<double> l, r;
        sine(l, 48000, 700.0, 0.3);
        r = l;
        const double p[] = {1.0, 2.0, 100.0, 50.0, 60.0};
        run(f, l, &r, p);
        double lo = 1e9, hi = 0;
        for (size_t b = 8000; b + 2000 < l.size(); b += 2000) {
            const double v = rms(l, b, b + 2000);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        check(hi > lo * 1.2, "Depth sweeps the notch",
              fmt("(block rms %.5f..%.5f)", lo, hi).c_str());
    }

    // The channels run the LFO half a turn apart, so they must differ.
    {
        Flanger f;
        f.prepare(kRate, kBlock);
        f.setEngaged(true);
        std::vector<double> l, r;
        sine(l, 24000, 700.0, 0.3);
        r = l;
        const double p[] = {1.0, 1.0, 90.0, 50.0, 50.0};
        run(f, l, &r, p);
        double diff = 0;
        for (size_t i = 4000; i < l.size(); ++i)
            diff = std::max(diff, std::fabs(l[i] - r[i]));
        check(diff > 0.01, "the two channels are in antiphase",
              fmt("(max |L-R| %.4f)", diff).c_str());
    }

    const double golden[] = {1.0, 0.9, 55.0, 25.0, -70.0};
    checkGolden("Flanger", goldenHash<Flanger>(golden, true), 0xDE9B5536AB18752Cull);
}

//--- Delay --------------------------------------------------------------------------------------
void checkDelay()
{
    printf("  delay\n");
    // The first repeat must arrive when the Time knob says it will. An impulse in, and the largest
    // peak after the dry one is the repeat.
    {
        Delay d;
        d.setTempo(0.0);
        d.prepare(kRate, kBlock);
        d.setEngaged(true);
        std::vector<double> l(24000, 0.0), r(24000, 0.0);
        l[100] = 1.0;
        r[100] = 1.0;
        const double timeMs = 200.0;
        const double p[] = {1.0, timeMs, 50.0, 10.0, 100.0, 0.0, 0.0};
        run(d, l, &r, p);
        size_t at = 0;
        double best = 0;
        for (size_t i = 300; i < l.size(); ++i)
            if (std::fabs(l[i]) > best) {
                best = std::fabs(l[i]);
                at = i;
            }
        const double gotMs = (double(at) - 100.0) * 1000.0 / kRate;
        check(std::fabs(gotMs - timeMs) < 6.0, "the first repeat lands on the Time knob",
              fmt("(asked %.0f ms, got %.1f ms)", timeMs, gotMs).c_str());
    }

    // Feedback must be bounded: at the knob's maximum the tail decays rather than running away.
    // The knob stops at 95 for exactly this reason — see the table in pedalids.h.
    {
        Delay d;
        d.setTempo(0.0);
        d.prepare(kRate, kBlock);
        d.setEngaged(true);
        std::vector<double> l(240000, 0.0), r(240000, 0.0);
        l[100] = 1.0;
        r[100] = 1.0;
        const double p[] = {1.0, 100.0, 95.0, 5.0, 100.0, 0.0, 0.0};
        run(d, l, &r, p);
        const double early = peak(l, 4800, 14400);
        const double late = peak(l, 220000, 240000);
        check(late < early, "the tail decays at maximum feedback",
              fmt("(peak %.4f early -> %.4f late)", early, late).c_str());
    }

    // Mix at zero is the dry signal, sample for sample.
    {
        Delay d;
        d.setTempo(0.0);
        d.prepare(kRate, kBlock);
        d.setEngaged(true);
        std::vector<double> l, r, dry;
        sine(l, 8000, 440.0, 0.3);
        r = l;
        dry = l;
        const double p[] = {1.0, 200.0, 50.0, 5.0, 0.0, 0.0, 0.0};
        run(d, l, &r, p);
        double worst = 0;
        for (size_t i = 0; i < l.size(); ++i)
            worst = std::max(worst, std::fabs(l[i] - dry[i]));
        check(worst < 1e-12, "Mix 0 passes the dry signal",
              fmt("(worst |wet-dry| %.3g)", worst).c_str());
    }

    // Sync: at 120 BPM a quarter note is 500 ms, and the division must be used INSTEAD of the
    // Time knob. Index 4 is "1/4" — asserted against the table rather than written as a literal.
    {
        int quarter = -1;
        for (int i = 0; i < kDelaySyncCount; ++i)
            if (strcmp(kDelaySyncNames[i], "1/4") == 0)
                quarter = i;
        Delay d;
        d.setTempo(120.0);
        d.prepare(kRate, kBlock);
        d.setEngaged(true);
        std::vector<double> l(48000, 0.0), r(48000, 0.0);
        l[100] = 1.0;
        r[100] = 1.0;
        // Time says 50 ms and must be ignored while Sync names a division.
        const double p[] = {1.0, 50.0, 50.0, 10.0, 100.0, double(quarter), 0.0};
        run(d, l, &r, p);
        size_t at = 0;
        double best = 0;
        for (size_t i = 300; i < l.size(); ++i)
            if (std::fabs(l[i]) > best) {
                best = std::fabs(l[i]);
                at = i;
            }
        const double gotMs = (double(at) - 100.0) * 1000.0 / kRate;
        check(quarter > 0 && std::fabs(gotMs - 500.0) < 12.0,
              "Sync overrides Time at the host's tempo",
              fmt("(120 BPM 1/4 = 500 ms, got %.1f ms)", gotMs).c_str());
    }

    const double golden[] = {1.0, 180.0, 60.0, 6.5, 45.0, 0.0, 0.0};
    checkGolden("Delay", goldenHash<Delay>(golden, true, [](Delay &d) { d.setTempo(0.0); }),
                0xF3268B7483EB96EFull);
}

//--- Reverb -------------------------------------------------------------------------------------
void checkReverb()
{
    printf("  reverb\n");
    // A longer Decay must mean a longer tail. Measured as the time the tail takes to fall 40 dB
    // below its own early peak, which is a robust proxy for T60 in a short buffer.
    double t40[2] = {0, 0};
    const double decays[2] = {1.0, 9.0};
    for (int k = 0; k < 2; ++k) {
        Reverb v;
        v.prepare(kRate, kBlock);
        v.setEngaged(true);
        std::vector<double> l(kRate * 8, 0.0), r(l.size(), 0.0);
        l[100] = 1.0;
        r[100] = 1.0;
        const double p[] = {1.0, decays[k], 5.0, 0.0, 100.0};
        run(v, l, &r, p);
        const double early = peak(l, 2400, 9600);
        const double floorLevel = early * 0.01; // -40 dB
        size_t last = 0;
        for (size_t i = 9600; i < l.size(); ++i)
            if (std::fabs(l[i]) > floorLevel)
                last = i;
        t40[k] = double(last) / kRate;
    }
    check(t40[1] > t40[0] * 1.4, "Decay lengthens the tail",
          fmt("(-40 dB at %.2f s for Decay 1 -> %.2f s for Decay 9)", t40[0], t40[1]).c_str());

    // Pre-delay must actually delay the onset of the wet signal.
    {
        Reverb v;
        v.prepare(kRate, kBlock);
        v.setEngaged(true);
        std::vector<double> l(24000, 0.0), r(24000, 0.0);
        l[100] = 1.0;
        r[100] = 1.0;
        const double preMs = 150.0;
        const double p[] = {1.0, 5.0, 5.0, preMs, 100.0};
        run(v, l, &r, p);
        size_t onset = 0;
        for (size_t i = 200; i < l.size(); ++i)
            if (std::fabs(l[i]) > 1e-4) {
                onset = i;
                break;
            }
        const double gotMs = (double(onset) - 100.0) * 1000.0 / kRate;
        check(gotMs > preMs * 0.8, "Pre-delay holds the wet signal back",
              fmt("(asked %.0f ms, wet starts at %.1f ms)", preMs, gotMs).c_str());
    }

    // Mix at zero is the dry signal, sample for sample.
    {
        Reverb v;
        v.prepare(kRate, kBlock);
        v.setEngaged(true);
        std::vector<double> l, r, dry;
        sine(l, 8000, 440.0, 0.3);
        r = l;
        dry = l;
        const double p[] = {1.0, 5.0, 5.0, 20.0, 0.0};
        run(v, l, &r, p);
        double worst = 0;
        for (size_t i = 0; i < l.size(); ++i)
            worst = std::max(worst, std::fabs(l[i] - dry[i]));
        check(worst < 1e-12, "Mix 0 passes the dry signal",
              fmt("(worst |wet-dry| %.3g)", worst).c_str());
    }

    const double golden[] = {1.0, 6.5, 7.0, 45.0, 60.0};
    checkGolden("Reverb", goldenHash<Reverb>(golden, true), 0x0AD37B196EE6ED56ull);
}

//--- the engage ramp, which is the base class's and is therefore checked once -------------------
void checkEngageRamp()
{
    printf("  footswitch\n");
    // Engaging must not step. The Boost is used because it is the pedal whose output differs most
    // from its input, so a hard mix change would show most clearly.
    //
    // AGAINST THE WET SIGNAL'S OWN SLEW, not the dry input's. That distinction is the whole test:
    // a clipped 220 Hz tone at Drive 9 and Level 9 has square-ish edges and slews far faster than
    // the sine that went in, so comparing the ramp against the INPUT's slew fails a pedal that is
    // behaving perfectly. Measured on this build: dry region 0.0086 per sample, the 8 ms engage
    // ramp 0.0280, the steady engaged signal 0.0452. The ramp is the quietest of the three, which
    // is the result a cross-fade is supposed to give.
    Boost b;
    b.prepare(kRate, kBlock);
    std::vector<double> l;
    sine(l, 24000, 220.0, 0.3);
    const double p[] = {1.0, 9.0, 5.0, 9.0};
    const int kStompAt = 8000;
    for (int i = 0; i < int(l.size()); i += kBlock) {
        b.setEngaged(i >= kStompAt); // stamp on it part-way through
        b.setParams(p);
        b.process(l.data() + i, nullptr, std::min(kBlock, int(l.size()) - i));
    }
    // The ramp is kEngageMs long; look at a window generously wider than that.
    const int rampEnd = kStompAt + int(kEngageMs * 0.001 * kRate) * 2;
    double rampStep = 0, steadyStep = 0;
    for (int i = kStompAt + 1; i < rampEnd; ++i)
        rampStep = std::max(rampStep, std::fabs(l[size_t(i)] - l[size_t(i - 1)]));
    for (size_t i = size_t(rampEnd) + 1; i < l.size(); ++i)
        steadyStep = std::max(steadyStep, std::fabs(l[i] - l[i - 1]));
    check(rampStep <= steadyStep, "engaging does not click",
          fmt("(largest step during the ramp %.5f, steady engaged %.5f)", rampStep, steadyStep)
              .c_str());

    // And a disengaged pedal must return the dry signal exactly once its ramp has landed.
    Boost c;
    c.prepare(kRate, kBlock);
    c.setEngaged(false);
    std::vector<double> m, dry;
    sine(m, 8000, 220.0, 0.3);
    dry = m;
    run(c, m, nullptr, p);
    double worst = 0;
    for (size_t i = 0; i < m.size(); ++i)
        worst = std::max(worst, std::fabs(m[i] - dry[i]));
    check(worst < 1e-12, "switched off, the pedal is out of circuit",
          fmt("(worst |out-in| %.3g)", worst).c_str());
}

} // namespace

int main()
{
    printf("pedalcheck: %d Hz, %d-sample blocks\n", int(kRate), kBlock);
    checkBoost();
    checkChorus();
    checkFlanger();
    checkDelay();
    checkReverb();
    checkEngageRamp();
    printf("pedalcheck: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
