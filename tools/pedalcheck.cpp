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
//   THE HASHES ARE A GLIBC MEASUREMENT, and they are only a gate on a glibc build. See the note
//   above checkGolden: the cross-platform check is --dump / --reference, not the hash.
//
// No VST3 and no host: this links the pedal headers directly, and it is therefore the one DSP
// check that runs on Windows — under Wine, with no host and no display.

#include "common/pedalids.h"
#include "pedals/boost.h"
#include "pedals/chorus.h"
#include "pedals/delay.h"
#include "pedals/flanger.h"
#include "pedals/reverb.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace Rations;
using namespace Rations::pedals;

//--- allocation tracking ------------------------------------------------------------------------
// The real-time contract says prepare() does every allocation and process()/setParams()/
// setEngaged() do none. That is a property of the machine code, not of the source reading well, so
// it is checked by counting: global operator new is replaced with one that increments a counter
// while armed, and the counter must stay at zero across a run that exercises every audio-path
// entry point on every pedal.
//
// The counter is armed only after a warm-up run, because the FIRST block through a pedal is
// entitled to touch pages the allocator has not faulted in yet, and because stdio itself allocates
// on first use. Nothing prints while armed.
namespace alloc
{
std::size_t gCount = 0;
bool gArmed = false;
} // namespace alloc

void *operator new(std::size_t n)
{
    if (alloc::gArmed)
        ++alloc::gCount;
    void *p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
// GCC 14 targeting MinGW warns -Wmismatched-new-delete on the std::free() in each of these, at
// the point where it inlines one of them into a libstdc++ deallocate(). The pairing is right:
// every pointer that can reach here came from the operator new above, which is this program's
// only allocator. Native GCC 14 compiles the identical code silently. Suppressed rather than
// worked around — an indirection that hid the pointer from the optimiser would silence it too,
// and that is concealing a diagnostic rather than answering it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}
#pragma GCC diagnostic pop

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

// Run one pedal over the golden stimulus. `stereo` says whether the right channel is fed and
// kept, and must match how the pedal was hashed in the first place.
struct GoldenStream {
    bool stereo = false;
    std::vector<double> l, r;
};

template <typename P>
void goldenRun(const double *params, bool stereo, void (*prime)(P &), GoldenStream &out)
{
    P p;
    if (prime)
        prime(p);
    p.prepare(kRate, kBlock);
    p.setEngaged(true);
    out.stereo = stereo;
    goldenStimulus(out.l, out.r, kBlock * kGoldenBlocks);
    for (int b = 0; b < kGoldenBlocks; ++b) {
        p.setParams(params);
        p.process(out.l.data() + b * kBlock, stereo ? out.r.data() + b * kBlock : nullptr, kBlock);
    }
    if (!stereo)
        out.r.clear(); // never written, and hashing it would hash the stimulus
}

std::uint64_t goldenHash(const GoldenStream &s)
{
    if (!s.stereo)
        return fnv1a(s.l.data(), s.l.size() * sizeof(double));
    // One run of bytes, left then right, which is the order the hashes were first taken in.
    std::vector<double> both;
    both.reserve(s.l.size() + s.r.size());
    both.insert(both.end(), s.l.begin(), s.l.end());
    both.insert(both.end(), s.r.begin(), s.r.end());
    return fnv1a(both.data(), both.size() * sizeof(double));
}

//--- the reference stream ------------------------------------------------------------------------
// THE HASHES ABOVE ARE A GLIBC MEASUREMENT, and a hash is all-or-nothing. Four of the five pedals
// reach libm on the audio path — sin() for the Chorus's and Flanger's LFOs, exp() and pow() for
// the Delay's tone smoother and the Reverb's decay coefficients — and MinGW's libm is not glibc's.
// The two disagree in the last bit or two, which is within what either promises and is nowhere
// near audible, and the hash turns that into a total mismatch. Measured on this machine, the
// Windows build reproduces Boost bit-for-bit and misses on the other four.
//
// Reporting those as failures would teach everyone to ignore the one check that says the sound
// changed, and moving the DSP off libm to make them agree would change what the pedals sound like
// on Linux, which is the thing that must not move. So the hash gates the reference platform and
// something else gates the port: --dump writes the five streams from a reference build, and
// --reference reads that file back and reports how far this build is from it, sample by sample.
// scripts/makedist-windows.sh runs both halves, so the Windows bundles are gated on the
// comparison.
//
// THE CAP IS MEASURED, NOT CHOSEN, and every run prints what it measured. Against a native build
// of this tree, the MinGW build under Wine (GCC 14, cairo/FreeType sysroot aside — this tool
// links neither) came out at:
//
//     Boost      0            byte-identical
//     Chorus     1.110e-16    of a 0.408 peak
//     Flanger    2.220e-16    of a 0.624 peak
//     Delay      1.110e-16    of a 0.420 peak
//     Reverb     2.220e-16    of a 0.615 peak
//
// which is one or two ULPs of a double, after 0.32 s of feedback in the two pedals that have any.
// The cap below is seven orders of magnitude above that and about 180 dB below the signal, so an
// ordinary libm revision cannot trip it and nothing structural — a mis-sized buffer, a channel
// read that was never written, a smoother that was not reset — can hide under it.
constexpr double kStreamMaxAbs = 1.0e-9;

// glibc is the reference libm: the hashes were recorded against it, and against rations-amp built
// the same way. Everywhere else the hash is reported rather than enforced.
#if defined(__GLIBC__)
constexpr bool kHashGates = true;
#else
constexpr bool kHashGates = false;
#endif

const char kStreamMagic[8] = {'R', 'P', 'D', 'S', 'T', 'R', 'M', '1'};

FILE *gDumpFile = nullptr;
std::vector<std::pair<std::string, GoldenStream>> gRef;
bool gRefLoaded = false;
int gHashNotes = 0;

bool writeAll(FILE *f, const void *p, size_t n)
{
    return fwrite(p, 1, n, f) == n;
}

bool readAll(FILE *f, void *p, size_t n)
{
    return fread(p, 1, n, f) == n;
}

// name, then the two channel runs. Doubles are written as they sit in memory: both ends of this
// comparison are the same little-endian machine word for word, and a text format would round.
bool writeStream(FILE *f, const char *name, const GoldenStream &s)
{
    const std::uint32_t nameLen = static_cast<std::uint32_t>(strlen(name));
    const std::uint32_t count = static_cast<std::uint32_t>(s.l.size());
    const std::uint8_t stereo = s.stereo ? 1 : 0;
    if (!writeAll(f, &nameLen, sizeof(nameLen)) || !writeAll(f, name, nameLen) ||
        !writeAll(f, &stereo, sizeof(stereo)) || !writeAll(f, &count, sizeof(count)))
        return false;
    if (!writeAll(f, s.l.data(), s.l.size() * sizeof(double)))
        return false;
    if (s.stereo && !writeAll(f, s.r.data(), s.r.size() * sizeof(double)))
        return false;
    return true;
}

// Untrusted input, like every other file this project reads: the counts are bounded against what
// this build would produce before anything is resized to them.
bool readStreams(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "pedalcheck: cannot open reference stream %s\n", path);
        return false;
    }
    char magic[8];
    if (!readAll(f, magic, sizeof(magic)) || memcmp(magic, kStreamMagic, sizeof(magic)) != 0) {
        fprintf(stderr, "pedalcheck: %s is not a pedalcheck reference stream\n", path);
        fclose(f);
        return false;
    }
    const std::uint32_t expected = static_cast<std::uint32_t>(kBlock * kGoldenBlocks);
    for (;;) {
        std::uint32_t nameLen = 0;
        if (fread(&nameLen, 1, sizeof(nameLen), f) == 0)
            break; // clean end of file
        if (nameLen == 0 || nameLen > 64) {
            fprintf(stderr, "pedalcheck: %s has a malformed record\n", path);
            fclose(f);
            return false;
        }
        std::string name(nameLen, '\0');
        std::uint8_t stereo = 0;
        std::uint32_t count = 0;
        if (!readAll(f, &name[0], nameLen) || !readAll(f, &stereo, sizeof(stereo)) ||
            !readAll(f, &count, sizeof(count)) || count != expected || stereo > 1) {
            fprintf(stderr, "pedalcheck: %s is truncated, or was written by another stimulus\n",
                    path);
            fclose(f);
            return false;
        }
        GoldenStream s;
        s.stereo = stereo != 0;
        s.l.resize(count);
        if (!readAll(f, s.l.data(), count * sizeof(double))) {
            fprintf(stderr, "pedalcheck: %s is truncated\n", path);
            fclose(f);
            return false;
        }
        if (s.stereo) {
            s.r.resize(count);
            if (!readAll(f, s.r.data(), count * sizeof(double))) {
                fprintf(stderr, "pedalcheck: %s is truncated\n", path);
                fclose(f);
                return false;
            }
        }
        gRef.emplace_back(name, std::move(s));
    }
    fclose(f);
    gRefLoaded = true;
    return true;
}

const GoldenStream *findRef(const char *name)
{
    for (const auto &e : gRef)
        if (e.first == name)
            return &e.second;
    return nullptr;
}

// The worst single-sample disagreement between this build and the reference one, over both
// channels. Reported alongside the reference's own peak, because "1e-16 out of 0.5" and "1e-16
// out of 1e-15" are not the same statement.
void checkAgainstReference(const char *name, const GoldenStream &s)
{
    const GoldenStream *ref = findRef(name);
    if (!ref) {
        check(false, "stream against the reference build", "not present in the reference file");
        return;
    }
    if (ref->stereo != s.stereo || ref->l.size() != s.l.size()) {
        check(false, "stream against the reference build", "the reference has a different shape");
        return;
    }
    double worst = 0.0, peak = 0.0;
    size_t at = 0;
    for (size_t i = 0; i < s.l.size(); ++i) {
        const double d = std::fabs(s.l[i] - ref->l[i]);
        if (d > worst) {
            worst = d;
            at = i;
        }
        peak = std::max(peak, std::fabs(ref->l[i]));
    }
    if (s.stereo) {
        for (size_t i = 0; i < s.r.size(); ++i) {
            const double d = std::fabs(s.r[i] - ref->r[i]);
            if (d > worst) {
                worst = d;
                at = i;
            }
            peak = std::max(peak, std::fabs(ref->r[i]));
        }
    }
    char detail[192];
    snprintf(detail, sizeof(detail),
             "(worst |diff| %.3e at sample %zu, reference peak %.3f, cap %.1e)", worst, at, peak,
             kStreamMaxAbs);
    check(worst <= kStreamMaxAbs, "stream against the reference build", detail);
}

// Run the pedal once, then make every claim there is to make about that one run: the hash where
// the hash means something, and the distance from the reference build where a file was given.
template <typename P>
void checkGolden(const char *name, const double *params, bool stereo, std::uint64_t want,
                 void (*prime)(P &) = nullptr)
{
    GoldenStream s;
    goldenRun<P>(params, stereo, prime, s);
    const std::uint64_t got = goldenHash(s);

    char detail[96];
    snprintf(detail, sizeof(detail), "0x%016llX", (unsigned long long)got);
    if (got == want) {
        check(true, "golden stream", detail);
    } else if (kHashGates) {
        char both[160];
        snprintf(both, sizeof(both), "got 0x%016llX, recorded 0x%016llX", (unsigned long long)got,
                 (unsigned long long)want);
        check(false, "golden stream", both);
        printf("          %s's DSP has changed. If that was deliberate, update the hash in this "
               "file\n"
               "          in the same commit and say what moved; if it was not, something in\n"
               "          dsp/sample.h or in the pedal itself has been altered.\n",
               name);
    } else {
        ++gHashNotes;
        printf("    note  golden stream 0x%016llX, recorded 0x%016llX on glibc - this build's "
               "libm\n"
               "          rounds differently, which the hash cannot tolerate and the reference\n"
               "          comparison can.\n",
               (unsigned long long)got, (unsigned long long)want);
    }

    if (gDumpFile && !writeStream(gDumpFile, name, s)) {
        fprintf(stderr, "pedalcheck: could not write %s to the dump file\n", name);
        ++gFailures;
    }
    if (gRefLoaded)
        checkAgainstReference(name, s);
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
    checkGolden<Boost>("Boost", golden, false, 0xD8AA5E0C672FB329ull);
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
    checkGolden<Chorus>("Chorus", golden, false, 0x91E022DE0A468313ull);
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
    checkGolden<Flanger>("Flanger", golden, true, 0xDE9B5536AB18752Cull);
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
    checkGolden<Delay>("Delay", golden, true, 0xF3268B7483EB96EFull,
                       [](Delay &d) { d.setTempo(0.0); });
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
    checkGolden<Reverb>("Reverb", golden, true, 0x0AD37B196EE6ED56ull);
}

//--- the engage ramp, which is the base class's and is therefore checked once -------------------
//--- the real-time contract ----------------------------------------------------------------------
// Every pedal, every audio-path entry point, zero allocations. See the note at the top of the file.
template <typename P>
std::size_t allocationsDuringProcess(const ParamList &list, bool stereo, void (*prime)(P &))
{
    // Plain-unit defaults straight out of the pedal's own table, with the footswitch forced on so
    // the DSP actually runs rather than sitting in the disengaged skip.
    double plain[kParamStateMax];
    for (int i = 0; i < list.count; ++i)
        plain[i] = list.items[i].def;
    plain[0] = 1.0;

    P p;
    if (prime)
        prime(p);
    p.prepare(kRate, kBlock);
    p.setEngaged(true);

    std::vector<double> l(static_cast<size_t>(kBlock)), r(static_cast<size_t>(kBlock));
    for (int i = 0; i < kBlock; ++i) {
        l[static_cast<size_t>(i)] = 0.25 * std::sin(2.0 * 3.14159265358979 * 220.0 * i / kRate);
        r[static_cast<size_t>(i)] = -l[static_cast<size_t>(i)];
    }

    // Warm-up, unarmed: the first blocks may fault in pages and are not what this is measuring.
    for (int b = 0; b < 8; ++b) {
        p.setParams(plain);
        p.process(l.data(), stereo ? r.data() : nullptr, kBlock);
    }

    alloc::gCount = 0;
    alloc::gArmed = true;
    for (int b = 0; b < 256; ++b) {
        // Move a knob every block — a host automating a parameter is the case where a smoother or
        // a coefficient update would be tempted to resize something.
        for (int i = 1; i < list.count; ++i) {
            const PedalParamSpec &sp = list.items[i];
            const double t = 0.5 + 0.5 * std::sin(0.05 * b + i);
            plain[i] = sp.min + t * (sp.max - sp.min);
        }
        p.setParams(plain);
        // Stomp the footswitch on and off across the run, which is what drives the engage ramp and
        // the reset-once-then-skip path.
        p.setEngaged((b / 32) % 2 == 0);
        p.process(l.data(), stereo ? r.data() : nullptr, kBlock);
    }
    // reset() is on the audio path too: it is what a host calls on a transport jump.
    p.reset();
    alloc::gArmed = false;
    return alloc::gCount;
}

void checkNoAllocations()
{
    printf("  real-time contract (no allocation on the audio path)\n");

    // NEGATIVE CONTROL, first: a counter that cannot count would report every pedal clean and
    // prove nothing. This allocates on purpose while armed and requires the counter to notice.
    {
        alloc::gCount = 0;
        alloc::gArmed = true;
        std::vector<double> *deliberate = new std::vector<double>(1024, 1.0);
        const std::size_t seen = alloc::gCount;
        alloc::gArmed = false;
        delete deliberate;
        check(seen > 0, "the allocation counter detects a deliberate allocation",
              seen > 0 ? "" : "(the checks below are meaningless)");
    }

    struct {
        const char *name;
        std::size_t count;
    } results[] = {
        {"Boost", allocationsDuringProcess<Boost>(kBoostParams, false, nullptr)},
        {"Chorus", allocationsDuringProcess<Chorus>(kChorusParams, true, nullptr)},
        {"Flanger", allocationsDuringProcess<Flanger>(kFlangerParams, true, nullptr)},
        {"Delay",
         allocationsDuringProcess<Delay>(kDelayParams, true, [](Delay &d) { d.setTempo(120.0); })},
        {"Reverb", allocationsDuringProcess<Reverb>(kReverbParams, true, nullptr)},
    };
    // fmt() takes doubles; these two fields are a string and a count, so they are formatted here.
    for (const auto &res : results) {
        char what[128], detail[64];
        snprintf(what, sizeof(what), "%s allocates nothing in process/setParams/setEngaged",
                 res.name);
        snprintf(detail, sizeof(detail), "(%zu allocation(s))", res.count);
        check(res.count == 0, what, detail);
    }
}

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

void usage()
{
    fprintf(stderr, "usage: pedalcheck [--dump FILE] [--reference FILE]\n"
                    "  --dump       write this build's five golden streams to FILE\n"
                    "  --reference  compare this build's streams against a FILE written by\n"
                    "               --dump on the reference build\n");
}

int main(int argc, char **argv)
{
    const char *dumpPath = nullptr;
    const char *refPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            dumpPath = argv[++i];
        else if (strcmp(argv[i], "--reference") == 0 && i + 1 < argc)
            refPath = argv[++i];
        else {
            usage();
            return 2;
        }
    }
    if (refPath && !readStreams(refPath))
        return 2;
    if (dumpPath) {
        gDumpFile = fopen(dumpPath, "wb");
        if (!gDumpFile) {
            fprintf(stderr, "pedalcheck: cannot write %s\n", dumpPath);
            return 2;
        }
        if (!writeAll(gDumpFile, kStreamMagic, sizeof(kStreamMagic))) {
            fprintf(stderr, "pedalcheck: cannot write %s\n", dumpPath);
            return 2;
        }
    }

    printf("pedalcheck: %d Hz, %d-sample blocks\n", int(kRate), kBlock);
    checkBoost();
    checkChorus();
    checkFlanger();
    checkDelay();
    checkReverb();
    checkEngageRamp();
    checkNoAllocations();

    if (gDumpFile) {
        if (fclose(gDumpFile) != 0) {
            fprintf(stderr, "pedalcheck: the dump file did not close cleanly\n");
            ++gFailures;
        } else {
            printf("pedalcheck: wrote the golden streams to %s\n", dumpPath);
        }
        gDumpFile = nullptr;
    }

    printf("pedalcheck: %d checks, %d failures\n", gChecks, gFailures);

    // A build whose libm is not the one the hashes were taken against, run with nothing to
    // compare itself to, has proved its behaviour and NOT proved its arithmetic. Say so: the
    // alternative is a green line that means less than it looks like it means.
    if (gHashNotes > 0 && !gRefLoaded) {
        printf("pedalcheck: WARNING: %d golden hash(es) differ because this build's libm is not\n"
               "  glibc's, and no --reference file was given, so nothing has checked this build's\n"
               "  output against the reference build's. Run the reference build with --dump.\n",
               gHashNotes);
    }
    return gFailures == 0 ? 0 : 1;
}
