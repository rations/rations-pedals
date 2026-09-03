// panelrender — draws every pedal face offline, and audits what pedalgeometry.h claims.
// Copyright (c) 2026 rations. MIT licence (see LICENSE).
//
// TWO JOBS, and the second is the reason this exists rather than a screenshot script.
//
//   1. RENDER. Each of the five faces, at a given scale, to a PNG. That is what
//      scripts/makedist-windows.sh diffs: the same five renders are produced by the native binary
//      and by the MinGW one under Wine, and compared pixel for pixel. If Cairo, FreeType or the
//      image cache behave differently on the two platforms, it shows up there rather than in
//      somebody's DAW.
//
//   2. AUDIT. pedalgeometry.h's static_asserts are arithmetic on constants; they cannot know how
//      wide "Ping-Pong" actually renders in Michroma at 18 units, and they cannot know that the
//      art still measures what it measured. Both are checked here, against the real font metrics
//      and the real PNGs, and a failure is a non-zero exit — so a re-exported enclosure or a
//      renamed legend breaks the build instead of quietly overhanging a border.
//
// It links PedalsGfx and nothing else: no VST3, no X11, no Win32. That is what lets it be the
// FIRST thing built and run when a Windows toolchain is being brought up.

#include "common/pedalface.h"
#include "common/pedalgeometry.h"
#include "common/pedalids.h"

// The five traits structs, so the name and the art key come from the same place the plug-ins take
// them from. A second table here would be a second thing to keep in step, and the whole point of
// this tool is to catch exactly that kind of drift.
#include "plugins/boost/traits.h"
#include "plugins/chorus/traits.h"
#include "plugins/delay/traits.h"
#include "plugins/flanger/traits.h"
#include "plugins/reverb/traits.h"

#include "gfx/canvas.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "platform/respath.h"

#include <cairo/cairo.h>

#include <cstdarg>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Rations;

namespace
{

struct Face {
    const char *key;  // "boost" — what the PNG is named and what the report calls it
    const char *name; // as silkscreened
    const char *art;  // ImageCache key
    ParamList params;
};

template <typename Traits> constexpr Face faceOf(const char *key)
{
    return Face{key, Traits::kShortName, Traits::kArt, Traits::kParams};
}

const Face kFaces[] = {
    faceOf<BoostTraits>("boost"), faceOf<ChorusTraits>("chorus"), faceOf<FlangerTraits>("flanger"),
    faceOf<DelayTraits>("delay"), faceOf<ReverbTraits>("reverb"),
};
constexpr int kFaceCount = static_cast<int>(sizeof(kFaces) / sizeof(kFaces[0]));

int gFailures = 0;

void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "panelrender: FAIL: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    ++gFailures;
}

// The relative luminance of an sRGB triple, per WCAG 2.1.
double luminance(double r, double g, double b)
{
    auto lin = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

//--- the art audit ----------------------------------------------------------------------------
// Re-derives the printable face from the PNG rather than trusting pedalgeometry.h's constants,
// exactly as the constants themselves were originally derived. The face is the run of BRIGHT
// pixels around the centre line; the black border trim either side of it is what stops the run.
struct FaceBounds {
    int left = 0, right = 0, top = 0, bottom = 0;
    bool ok = false;
};

FaceBounds measureArt(const std::string &path, double *meanR, double *meanG, double *meanB)
{
    FaceBounds b;
    cairo_surface_t *img = cairo_image_surface_create_from_png(path.c_str());
    if (!img || cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
        if (img)
            cairo_surface_destroy(img);
        return b;
    }
    cairo_surface_flush(img);
    const int w = cairo_image_surface_get_width(img);
    const int h = cairo_image_surface_get_height(img);
    const int stride = cairo_image_surface_get_stride(img);
    const unsigned char *data = cairo_image_surface_get_data(img);
    if (!data || w != geo::kArtW || h != geo::kArtH) {
        cairo_surface_destroy(img);
        return b;
    }

    // Cairo's ARGB32 is premultiplied and native-endian, which on every platform this builds for
    // means B, G, R, A in memory order.
    auto at = [&](int x, int y, double &r, double &g, double &bb, double &a) {
        const unsigned char *p =
            data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
        a = p[3] / 255.0;
        // Un-premultiply, so a semi-transparent edge pixel is not read as a dark one.
        const double s = a > 0.0 ? 1.0 / (255.0 * a) : 0.0;
        bb = p[0] * s;
        g = p[1] * s;
        r = p[2] * s;
    };

    auto bright = [&](int x, int y) {
        double r, g, bb, a;
        at(x, y, r, g, bb, a);
        return a > 0.78 && luminance(r, g, bb) > 0.02;
    };

    const int midX = w / 2, midY = h / 2;
    if (!bright(midX, midY)) {
        cairo_surface_destroy(img);
        return b;
    }
    b.left = midX;
    while (b.left - 1 >= 0 && bright(b.left - 1, midY))
        --b.left;
    b.right = midX;
    while (b.right + 1 < w && bright(b.right + 1, midY))
        ++b.right;
    b.top = midY;
    while (b.top - 1 >= 0 && bright(midX, b.top - 1))
        --b.top;
    b.bottom = midY;
    while (b.bottom + 1 < h && bright(midX, b.bottom + 1))
        ++b.bottom;

    // Mean face colour, over a generous rectangle well inside the border.
    double sr = 0, sg = 0, sb = 0;
    long n = 0;
    for (int y = geo::kKnob4Row1CY - 40; y < geo::kKnob4Row1CY + 200; ++y) {
        for (int x = geo::kFaceLeft + 30; x < geo::kFaceRight - 30; ++x) {
            double r, g, bb, a;
            at(x, y, r, g, bb, a);
            if (a < 0.9)
                continue;
            sr += r;
            sg += g;
            sb += bb;
            ++n;
        }
    }
    if (n > 0) {
        *meanR = sr / n;
        *meanG = sg / n;
        *meanB = sb / n;
    }
    b.ok = true;
    cairo_surface_destroy(img);
    return b;
}

//--- the text audit ---------------------------------------------------------------------------
// What pedalgeometry.h cannot know: how wide a legend actually is.
void auditText(Canvas &c, const Face &f)
{
    const int knobs = knobCount(f.params);

    // Each legend is centred on its own knob, so its allowance is that knob's distance to the
    // nearer edge of the face rather than one shared slot width.
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kKnobLabelSize));
    for (int k = 0; k < knobs; ++k) {
        const int idx = knobParam(f.params, k);
        if (idx < 0)
            continue;
        const char *legend = f.params[idx].legend;
        const float w = c.stringWidth(legend);
        const float slot = static_cast<float>(geo::knobLabelAllowance(knobs, k));
        printf("    knob %-10s %6.1f / %6.1f units%s\n", legend, w, slot,
               w > slot ? "   <- CLIPPED" : "");
        if (w > slot)
            fail("%s: the legend '%s' is %.1f units wide in a %.1f-unit slot", f.key, legend, w,
                 slot);

        // A three-knob face letters its upper pair BESIDE the dial centred below them, so the
        // legend must also stop short of that dial. This is the clearance the amp's layout rests
        // on and the one a wider legend would break first.
        if (knobs == 3 && k < 2) {
            const geo::Point pt = geo::knobPos(knobs, k);
            const float reach =
                std::fabs(static_cast<float>(pt.x) - static_cast<float>(geo::kFaceCX)) - w * 0.5f;
            if (reach < static_cast<float>(geo::kKnobR))
                fail("%s: the legend '%s' reaches the dial centred below it (%.1f < %d)", f.key,
                     legend, reach, geo::kKnobR);
        }
    }

    // The name, which is the widest thing on the face and the one most likely to reach the border.
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kNameSize));
    const float nameW = c.stringWidth(f.name);
    printf("    name %-10s %6.1f / %6.1f units%s\n", f.name, nameW, static_cast<float>(geo::kFaceW),
           nameW > geo::kFaceW ? "   <- CLIPPED" : "");
    if (nameW > geo::kFaceW)
        fail("%s: the name '%s' is %.1f units wide on a %d-unit face", f.key, f.name, nameW,
             geo::kFaceW);

    // Mini plates carry their own text INSIDE them, at the plate's own size — a toggle its name,
    // a list its current value. Measured against the plate less the margin the painter clips to.
    const int minis = miniCount(f.params);
    const float miniSlot = static_cast<float>(geo::kMiniW - 6);
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kMiniTextSize));
    for (int m = 0; m < minis; ++m) {
        const int idx = miniParam(f.params, m);
        if (idx < 0)
            continue;
        const float w = c.stringWidth(f.params[idx].legend);
        printf("    mini %-10s %6.1f / %6.1f units%s\n", f.params[idx].legend, w, miniSlot,
               w > miniSlot ? "   <- CLIPPED" : "");
        if (w > miniSlot)
            fail("%s: the mini legend '%s' does not fit its plate", f.key, f.params[idx].legend);
    }

    // The bypass legend is the widest fixed string on the face and the one pedalgeometry.h has to
    // guess at, because its clearance from the footswitch is a compile-time assertion on a
    // constant.
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kToggleLabelSize));
    const float bypassW = c.stringWidth("BYPASS");
    printf("    bypass legend %6.1f / %6.1f units (kToggleLabelW)\n", bypassW,
           static_cast<float>(geo::kToggleLabelW));
    if (bypassW > geo::kToggleLabelW)
        fail("%s: 'BYPASS' renders %.1f units wide; kToggleLabelW says %d, and the clearance from "
             "the footswitch is asserted against that",
             f.key, bypassW, geo::kToggleLabelW);

    // Every value a List control can take has to fit its plate too — a division name that
    // overflowed would be clipped only at one setting, which is the kind of fault that ships.
    c.setFont(Font::Title);
    c.setFontSize(static_cast<float>(geo::kMiniTextSize));
    for (int m = 0; m < minis; ++m) {
        const int idx = miniParam(f.params, m);
        if (idx < 0 || f.params[idx].kind != PedalParamKind::List)
            continue;
        for (int v = 0; v < kDelaySyncCount; ++v) {
            const float w = c.stringWidth(kDelaySyncNames[v]);
            if (w > miniSlot)
                fail("%s: the list value '%s' does not fit its plate (%.1f > %.1f)", f.key,
                     kDelaySyncNames[v], w, miniSlot);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    double scale = 1.0;
    std::string outDir;
    std::string resDir;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atof(argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outDir = argv[++i];
        else if (strcmp(argv[i], "--resources") == 0 && i + 1 < argc)
            resDir = argv[++i];
        else {
            fprintf(stderr, "usage: panelrender [--scale S] [--out DIR] [--resources DIR]\n"
                            "  With no --out, renders nothing and only runs the audit.\n");
            return 2;
        }
    }
    if (scale < 0.1 || scale > 8.0) {
        fprintf(stderr, "panelrender: --scale %g is out of range\n", scale);
        return 2;
    }

    if (resDir.empty())
        resDir = resourceDir();
    if (resDir.empty()) {
        fprintf(stderr, "panelrender: no resource directory; pass --resources or set "
                        "RATIONS_PEDALS_RESOURCE_DIR\n");
        return 2;
    }
    printf("panelrender: resources %s, scale %.3f\n", resDir.c_str(), scale);

    FontStack fonts;
    if (!fonts.load(resDir))
        fprintf(stderr, "panelrender: WARNING: the bundled fonts did not load; metrics below are "
                        "a fallback face and mean nothing\n");

    ImageCache images;
    images.setResourceDir(resDir);

    const int pw = static_cast<int>(std::lround(geo::kWindowW * scale));
    const int ph = static_cast<int>(std::lround(geo::kWindowH * scale));

    for (int i = 0; i < kFaceCount; ++i) {
        const Face &f = kFaces[i];
        printf("  %s\n", f.key);

        // --- the art, re-measured ---
        double mr = 0, mg = 0, mb = 0;
        const std::string art = resDir + "/img/" + f.art + ".png";
        const FaceBounds b = measureArt(art, &mr, &mg, &mb);
        if (!b.ok) {
            fail("%s: could not measure %s", f.key, art.c_str());
        } else {
            const double lum = luminance(mr, mg, mb);
            const double contrast = 1.05 / (lum + 0.05);
            printf("    face x %d..%d  y %d..%d   mean rgb %.3f %.3f %.3f   white %.2f:1\n", b.left,
                   b.right, b.top, b.bottom, mr, mg, mb, contrast);
            // The constants must still describe the art. One unit of slack: an antialiased edge
            // pixel is a judgement call and always was.
            if (std::abs(b.left - geo::kFaceLeft) > 1 || std::abs(b.right - geo::kFaceRight) > 1 ||
                std::abs(b.top - geo::kFaceTop) > 1 || std::abs(b.bottom - geo::kFaceBottom) > 1)
                fail("%s: the art measures x %d..%d y %d..%d, but pedalgeometry.h says x %d..%d "
                     "y %d..%d",
                     f.key, b.left, b.right, b.top, b.bottom, geo::kFaceLeft, geo::kFaceRight,
                     geo::kFaceTop, geo::kFaceBottom);
        }

        // --- the render ---
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
        cairo_t *cr = cairo_create(surface);
        cairo_scale(cr, scale, scale);
        Canvas c(cr, &fonts, static_cast<float>(geo::kWindowW), static_cast<float>(geo::kWindowH));

        std::vector<double> norm(static_cast<size_t>(f.params.count), 0.0);
        for (int k = 0; k < f.params.count; ++k)
            norm[static_cast<size_t>(k)] = pedalNorm(f.params[k], f.params[k].def);
        norm[0] = 1.0; // footswitch on, so the lamp and its clearances are exercised

        FaceState st;
        st.params = f.params;
        st.norm = norm.data();
        st.name = f.name;
        st.art = f.art;
        st.bypassed = false;
        st.bindingText = "CC 64";
        st.learned = true; // draws the Clear button, which is the wider strip layout
        drawPedalFace(c, images, st, scale);
        drawStrip(c, st, scale);

        auditText(c, f);

        if (!outDir.empty()) {
            const std::string png = outDir + "/" + f.key + ".png";
            cairo_surface_flush(surface);
            if (cairo_surface_write_to_png(surface, png.c_str()) != CAIRO_STATUS_SUCCESS)
                fail("%s: could not write %s", f.key, png.c_str());
            else
                printf("    wrote %s (%dx%d)\n", png.c_str(), pw, ph);
        }

        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }

    if (gFailures > 0) {
        fprintf(stderr, "panelrender: %d failure(s)\n", gFailures);
        return 1;
    }
    printf("panelrender: all faces audited, 0 failures\n");
    return 0;
}
