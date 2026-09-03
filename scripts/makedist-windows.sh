#!/usr/bin/env bash
# Cross-build Rations Pedals for 64-bit Windows and package the release ZIP into dist/.
#
# FIVE PRODUCTS ON WINDOWS, AND ONLY THE PLUG-INS. The standalones are JACK applications and
# therefore a Linux product; they ship with that release (scripts/makedist-linux.sh) and there is
# no second binary to package here.
#
# TWO WAYS TO INSTALL, both in the ZIP. RationsPedals-install.exe puts the bundles where hosts look,
# lets the user choose which of the five, and registers an uninstall entry per pedal; the five
# Rations<Name>.vst3 folders beside it are the same bundles for anyone who would rather copy them
# themselves — which is not a stylistic preference, it is the fallback for a machine whose
# SmartScreen or antivirus refuses an unsigned installer.
#
# NO WINDOWS MACHINE IS INVOLVED. The compiler is MinGW-w64 running here; the verification runs the
# cross-built binaries under Wine, which is enough to prove each bundle loads, passes the SDK
# validator, and computes and draws what the Linux build does. It is NOT a substitute for a test on
# real Windows, which is the actual gate before a release goes out.
#
# WHAT THIS GATES ON, per bundle: the architecture folder and the inner DLL's name; the bundle's
# own art and nobody else's; the import list (proving -static held); the export list (exactly the
# three entry points, catching a .drectve leak); the SDK validator under Wine. Then, once, for the
# whole build: the DSP compared against the native build sample by sample, and the five faces
# compared against the native render pixel by pixel.
#
# Environment:
#   WINEPREFIX               defaults to ~/.wine-rations-pedals — a prefix of its own, because a
#                            desktop's ~/.wine is usually managed by something else.
#   RPEDALS_SKIP_WINE        set to 1 to package without the Wine verification. The ZIP is then
#                            unverified AND has no moduleinfo.json; the script says so loudly
#                            rather than quietly producing a lesser build.
#   RPEDALS_SKIP_INSTALLER   set to 1 to package the bundles without RationsPedals-install.exe.
#   RPEDALS_NSIS_DIR         the NSIS prefix, defaulting to ~/third_party/nsis (bin/ and
#                            share/nsis/). A makensis on PATH is used if absent.
#   RPEDALS_BUILD_DIR        the NATIVE build directory, used as the reference for the DSP and
#                            panel comparisons. Defaults to ./build.
#   RPEDALS_WIN_SYSROOT      the MinGW dependency sysroot. Defaults to ~/third_party/win-deps/sysroot.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build-win"
NATIVE_BUILD="${RPEDALS_BUILD_DIR:-$REPO/build}"
TRIPLE="${RPEDALS_WIN_TRIPLE:-x86_64-w64-mingw32}"
OBJDUMP="$TRIPLE-objdump"
STRIP="$TRIPLE-strip"

# The five, in the order they are listed everywhere else in this project.
PEDALS=(boost chorus flanger delay reverb)
declare -A TARGET=([boost]=RationsBoost [chorus]=RationsChorus [flanger]=RationsFlanger \
                   [delay]=RationsDelay [reverb]=RationsReverb)

command -v "$TRIPLE-g++" >/dev/null || {
  echo "error: $TRIPLE-g++ not found. Install the MinGW-w64 cross toolchain:" >&2
  echo "  sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64" >&2
  exit 1
}
if [ ! -d "${RPEDALS_WIN_SYSROOT:-$HOME/third_party/win-deps/sysroot}/lib/pkgconfig" ]; then
  echo "error: the Windows dependency sysroot is missing. Build it first:" >&2
  echo "  scripts/build-win-deps.sh" >&2
  exit 1
fi

# makensis is a NATIVE Linux binary: it links one of NSIS's prebuilt PE stubs and appends the
# compressed payload, so the installer is produced without Wine and without the cross compiler.
# Prefer an unpacked prefix (bin/ + share/nsis/, which is what `apt-get download nsis nsis-common`
# + `dpkg-deb -x` gives without root); fall back to a system install.
MAKENSIS=""
NSIS_PREFIX="${RPEDALS_NSIS_DIR:-$HOME/third_party/nsis}"
if [ -x "$NSIS_PREFIX/bin/makensis" ] && [ -d "$NSIS_PREFIX/share/nsis" ]; then
  MAKENSIS="$NSIS_PREFIX/bin/makensis"
  export NSISDIR="$NSIS_PREFIX/share/nsis"
elif command -v makensis >/dev/null; then
  MAKENSIS="makensis"
fi
if [ "${RPEDALS_SKIP_INSTALLER:-0}" != "1" ] && [ -z "$MAKENSIS" ]; then
  echo "error: makensis not found, so RationsPedals-install.exe cannot be built." >&2
  echo "  sudo apt install nsis" >&2
  echo "or unpack it without root into \$RPEDALS_NSIS_DIR (default $NSIS_PREFIX) as" >&2
  echo "bin/makensis and share/nsis/:" >&2
  echo "  apt-get download nsis nsis-common && dpkg-deb -x <each>.deb root/" >&2
  echo "Set RPEDALS_SKIP_INSTALLER=1 to package the bundles without an installer." >&2
  exit 1
fi

cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-mingw-w64.cmake" -S "$REPO"
cmake --build "$BUILD" --parallel "$(nproc)"

# The version out of the project() call, read rather than duplicated, and read the SAME way
# makedist-linux.sh reads it so the two releases cannot be tagged differently from one another.
# Matched against project( specifically: cmake_minimum_required(VERSION 3.25.0) is above it, and a
# looser expression tags the release 3.25.0 without saying anything.
VERSION="$(sed -n 's/^[[:space:]]*project(.*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' \
  "$REPO/CMakeLists.txt" | head -1)"
if [ -z "$VERSION" ]; then
  echo "could not read the project version from CMakeLists.txt" >&2
  exit 1
fi

STAGEDIR="$(mktemp -d)"
PKGDIR="$STAGEDIR/RationsPedals-${VERSION}"
mkdir -p "$PKGDIR"
trap 'rm -rf "$STAGEDIR"' EXIT

# --- the five bundles -------------------------------------------------------
for pedal in "${PEDALS[@]}"; do
  name="${TARGET[$pedal]}"

  BUNDLE="$BUILD/VST3/Release/${name}.vst3"
  if [ ! -d "$BUNDLE" ]; then
    echo "VST3 bundle not found at $BUNDLE" >&2
    exit 1
  fi
  cp -r "$BUNDLE" "$PKGDIR/"
  PKGBUNDLE="$PKGDIR/${name}.vst3"

  # ONE ARCHITECTURE FOLDER, AND IT IS THE WINDOWS ONE.
  #
  # A VST3 bundle holds Contents/<arch>/ per platform, so a build tree that was once configured
  # natively and later re-configured with the MinGW toolchain keeps its Contents/x86_64-linux
  # directory: CMake writes the new binary beside the old one instead of replacing it, and `cp -r`
  # then carries a Linux .so into the Windows ZIP. It loads nowhere and is pure weight.
  for _arch in "$PKGBUNDLE/Contents"/*/; do
    _arch="${_arch%/}"
    _aname="$(basename "$_arch")"
    case "$_aname" in
      Resources | x86_64-win) ;;
      *)
        echo "warning: removing $_aname/ from ${name}.vst3 — it is not a Windows architecture" >&2
        echo "  folder. '$BUILD' was configured for another platform at some point; run" >&2
        echo "  'rm -rf $BUILD' and re-run this script to stop seeing this." >&2
        rm -rf "$_arch"
        ;;
    esac
  done

  # SHAPE. Module::validateBundleStructure in the SDK's module_win32.cpp requires the inner DLL to
  # be named exactly like the bundle folder — RationsBoost.vst3 inside
  # RationsBoost.vst3/Contents/x86_64-win/. A DLL called RationsBoost.dll in the right folder does
  # not load, and nothing before this point would have said so: the CMake bundle machinery places
  # the binary with a foreach over CMAKE_CONFIGURATION_TYPES, which is EMPTY under every
  # single-config generator, so getting this wrong produces a complete-looking bundle with the
  # binary somewhere else entirely.
  DLL="$PKGBUNDLE/Contents/x86_64-win/${name}.vst3"
  if [ ! -f "$DLL" ]; then
    echo "no ${name}.vst3 binary inside $PKGBUNDLE/Contents/x86_64-win/" >&2
    echo "The bundle layout is wrong; see the LIBRARY_OUTPUT_DIRECTORY block in CMakeLists.txt." >&2
    echo "No host can load this." >&2
    find "$PKGBUNDLE" -type f >&2
    exit 1
  fi

  # RESOURCES. This project embeds NO fallback art: src/gfx/resourcestore.h states that its
  # built-in table is always empty here by design. So a bundle that reaches a user without
  # Contents/Resources draws flat rectangles and says so only on stderr, which nobody reads.
  #
  # ITS OWN ENCLOSURE IS THE POINT of checking this per pedal rather than once: the five bundles
  # are built by one loop from one resource list plus one per-pedal file, and a mistake in that
  # loop would show up as four correct bundles and one drawing somebody else's pedal.
  for _res in "Contents/Resources/img/pedal-${pedal}.png" \
              Contents/Resources/img/dial.png \
              Contents/Resources/img/pedal_switch.png \
              Contents/Resources/img/led_on.png \
              Contents/Resources/img/led_off.png \
              Contents/Resources/img/switch_up_ring.png \
              Contents/Resources/img/switch_down_ring.png \
              Contents/Resources/fonts/Michroma-Regular.ttf \
              Contents/Resources/fonts/Roboto-Regular.ttf; do
    if [ ! -f "$PKGBUNDLE/$_res" ]; then
      echo "${name}.vst3 is missing $_res; the editor would draw flat rectangles." >&2
      exit 1
    fi
  done

  # ...and NOT another pedal's. Cheap, and it is the other half of the check above.
  for _other in "${PEDALS[@]}"; do
    [ "$_other" = "$pedal" ] && continue
    if [ -f "$PKGBUNDLE/Contents/Resources/img/pedal-${_other}.png" ]; then
      echo "${name}.vst3 carries pedal-${_other}.png, which it never draws." >&2
      exit 1
    fi
  done

  "$STRIP" --strip-unneeded "$DLL"

  # IMPORTS. Nothing but Windows' own DLLs may appear here.
  #
  # The MinGW toolchain's default is to link libgcc_s_seh-1.dll, libstdc++-6.dll and
  # libwinpthread-1.dll, and a VST3 bundle CANNOT ship those beside the binary: the SDK loads a
  # plug-in with a plain LoadLibraryW of the full path (module_win32.cpp, loadAsPackage) and the
  # default DLL search order does not include the loaded module's own directory. The -static in
  # cmake/toolchain-mingw-w64.cmake is what prevents it, it is one edit away from being lost, and
  # losing it fails on the user's machine — as a plug-in that simply never appears — rather than
  # here.
  ALLOWED_DLLS='^(KERNEL32|USER32|GDI32|MSIMG32|SHELL32|ADVAPI32|ole32|OLEAUT32|COMDLG32|WINSPOOL|SHLWAPI|msvcrt|api-ms-win-)'
  BAD_IMPORTS="$("$OBJDUMP" -p "$DLL" | sed -n 's/^\tDLL Name: //p' \
    | sed 's/\.dll$//I' | grep -Ev "$ALLOWED_DLLS" || true)"
  if [ -n "$BAD_IMPORTS" ]; then
    echo "${name}.vst3 imports non-system DLLs:" >&2
    echo "$BAD_IMPORTS" >&2
    echo "A VST3 bundle cannot ship sibling DLLs — the host LoadLibraryW's the plug-in by full" >&2
    echo "path and its own directory is not searched. Check that -static survived in" >&2
    echo "cmake/toolchain-mingw-w64.cmake." >&2
    exit 1
  fi

  # EXPORTS. Exactly three, no more.
  #
  # The three are what a VST3 host calls. "No more" is the part worth checking, and it is worth it
  # twice over here: a static archive built with __declspec(dllexport) still in effect carries
  # .drectve export directives that the linker obeys, so linking one silently re-exports somebody
  # else's entire API from our plug-in — and -Wl,--exclude-all-symbols does NOT catch it, because a
  # .drectve export is explicit rather than automatic. FreeType's meson build does exactly that on
  # Windows, which is why scripts/build-win-deps.sh removes it. With FIVE sibling plug-ins loaded
  # into one host process, a leaked export list is also five plug-ins offering the same symbols to
  # a loader that will pick one of them for everybody.
  EXPORTS="$("$OBJDUMP" -p "$DLL" \
    | sed -n '/\[Ordinal\/Name Pointer\] Table/,$p' \
    | sed -n 's/^\t\[ *[0-9]* *\] +base\[ *[0-9]* *\] *[0-9a-f]* \(.*\)$/\1/p' \
    | sort)"
  EXPECTED="$(printf 'ExitDll\nGetPluginFactory\nInitDll\n')"
  if [ "$EXPORTS" != "$EXPECTED" ]; then
    echo "${name}.vst3 does not export exactly the three entry points." >&2
    echo "expected:" >&2; echo "$EXPECTED" | sed 's/^/  /' >&2
    echo "found ($(echo "$EXPORTS" | grep -c .)):" >&2
    echo "$EXPORTS" | head -20 | sed 's/^/  /' >&2
    [ "$(echo "$EXPORTS" | grep -c .)" -gt 20 ] && echo "  ..." >&2
    exit 1
  fi
done

# --- verification under Wine ------------------------------------------------
if [ "${RPEDALS_SKIP_WINE:-0}" = "1" ]; then
  echo
  echo "WARNING: RPEDALS_SKIP_WINE=1 — the SDK validator was NOT run against these bundles," >&2
  echo "the DSP was NOT compared against the native build, the faces were NOT compared against" >&2
  echo "the native render, and no moduleinfo.json was generated. Do not release this." >&2
  echo
else
  command -v wine >/dev/null || {
    echo "error: wine not found. Install it, or set RPEDALS_SKIP_WINE=1 to package an" >&2
    echo "unverified build." >&2
    exit 1
  }
  export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-rations-pedals}"
  export WINEDEBUG="${WINEDEBUG:--all}"
  if [ ! -d "$WINEPREFIX" ]; then
    echo "creating a Wine prefix at $WINEPREFIX"
    wineboot --init >/dev/null 2>&1 || true
  fi

  for pedal in "${PEDALS[@]}"; do
    name="${TARGET[$pedal]}"
    PKGBUNDLE="$PKGDIR/${name}.vst3"
    WIN_BUNDLE="$(winepath -w "$PKGBUNDLE")"

    # moduleinfo.json is OPTIONAL — Module::getModuleInfoPath returns an empty optional when it is
    # absent and the validator does not require it — but it is one Wine call, so generate it. It
    # has to happen here rather than as a build step because CMake would run the freshly
    # cross-built moduleinfotool.exe natively on the build host, where it cannot execute.
    wine "$BUILD/bin/moduleinfotool.exe" -create -version "$VERSION" \
         -path "$WIN_BUNDLE" \
         -output "$(winepath -w "$PKGBUNDLE/Contents/Resources/moduleinfo.json")" 2>/dev/null
    if [ ! -s "$PKGBUNDLE/Contents/Resources/moduleinfo.json" ]; then
      echo "moduleinfotool produced no moduleinfo.json for ${name}" >&2
      exit 1
    fi

    VALIDATOR_OUT="$(wine "$BUILD/bin/validator.exe" "$WIN_BUNDLE" 2>&1 || true)"
    if ! printf '%s' "$VALIDATOR_OUT" | grep -qE '^Result: [0-9]+ tests passed, 0 tests failed'; then
      echo "the SDK validator did not pass against ${name}.vst3:" >&2
      printf '%s\n' "$VALIDATOR_OUT" | tail -40 >&2
      exit 1
    fi
    printf '  %-16s %s\n' "${name}.vst3" "$(printf '%s' "$VALIDATOR_OUT" | grep -E '^Result:')"
  done

  # The two comparisons below both need a NATIVE build of this same tree to compare against.
  if [ ! -x "$NATIVE_BUILD/pedalcheck" ] || [ ! -x "$NATIVE_BUILD/panelrender" ]; then
    echo "no native pedalcheck/panelrender in $NATIVE_BUILD — build the native tree first:" >&2
    echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
    echo "or set RPEDALS_BUILD_DIR to a native build directory." >&2
    exit 1
  fi

  #-------------------------------------------------------------------------
  # DOES THE WINDOWS BUILD COMPUTE WHAT THE LINUX ONE COMPUTES?
  #
  # Not to the bit, and it cannot be: four of the five pedals reach libm on the audio path — sin()
  # for the Chorus's and Flanger's LFOs, exp() and pow() for the Delay's tone smoother and the
  # Reverb's decay coefficients — and MinGW's libm is not glibc's. They disagree in the last bit or
  # two, which is legal for both and inaudible, and the golden hashes in tools/pedalcheck.cpp turn
  # that into a total mismatch. So the reference build dumps its five streams and the Windows build
  # is measured against them sample by sample; the cap is in pedalcheck.cpp with the measurement it
  # was taken from. This runs the WHOLE of pedalcheck under Wine, so every behavioural claim about
  # every pedal is also re-made against the cross-built DSP.
  #-------------------------------------------------------------------------
  echo "comparing the DSP against the native build"
  REF_STREAMS="$STAGEDIR/reference.streams"
  "$NATIVE_BUILD/pedalcheck" --dump "$REF_STREAMS" >/dev/null
  if ! wine "$BUILD/pedalcheck.exe" --reference "$(winepath -w "$REF_STREAMS")" \
       > "$STAGEDIR/pedalcheck-win.txt" 2>&1; then
    echo "the cross-built DSP does not match the native build:" >&2
    grep -E 'FAIL|WARNING|failures' "$STAGEDIR/pedalcheck-win.txt" >&2 || \
      tail -20 "$STAGEDIR/pedalcheck-win.txt" >&2
    exit 1
  fi
  grep -E 'stream against the reference build' "$STAGEDIR/pedalcheck-win.txt" | sed 's/^ */  /'
  grep -E '^pedalcheck: [0-9]+ checks' "$STAGEDIR/pedalcheck-win.txt" | sed 's/^/  /'

  #-------------------------------------------------------------------------
  # DOES THE WINDOWS EDITOR DRAW WHAT THE LINUX ONE DRAWS?
  #
  # This is the only automated check of the Windows editor's appearance, and it is why
  # scripts/build-win-deps.sh pins its five dependencies to this machine's system versions. It
  # renders all five faces with both panelrender binaries — the same sources, the same resources/
  # tree, the same scale — and compares them pixel for pixel. It therefore exercises cairo,
  # FreeType rasterisation, PNG decode and the whole of src/gfx in the cross build, plus
  # panelrender's own art, text-clearance and hit-target audits.
  #
  # THE THRESHOLDS ARE MEASURED, NOT CHOSEN. As first taken, at cairo 1.18.4 / freetype 2.13.3 /
  # pixman 0.44.0 / libpng 1.6.48 / zlib 1.3.1, all five faces came out BYTE-IDENTICAL: 0 of
  # 356,148 pixels differing on each. The caps below sit well above that so an ordinary art edit
  # does not trip them, while a genuine divergence stays impossible to miss — a different FreeType
  # or cairo moves glyph rasterisation by thousands of pixels at full contrast.
  #
  # MAX_DELTA IS THE SHARPER OF THE TWO GATES and the one to trust. A rounding difference is 1/255
  # by definition; a real rasterisation change puts down ink where there was none, which is a delta
  # of tens or hundreds whatever the pixel count says.
  #-------------------------------------------------------------------------
  PANEL_MAX_PIXELS=256     # per face
  PANEL_MAX_DELTA=1        # per channel, any face

  if ! command -v magick >/dev/null; then
    echo >&2
    echo "WARNING: ImageMagick (magick) not found, so the Linux-vs-Windows panel comparison was" >&2
    echo "SKIPPED. Nothing has checked that the Windows editor draws what the Linux one draws." >&2
    echo "Install ImageMagick 7 and re-run." >&2
    echo >&2
  else
    echo "comparing the faces against the native render"
    PANELS="$STAGEDIR/panels"
    mkdir -p "$PANELS/lin" "$PANELS/win"

    "$NATIVE_BUILD/panelrender" --out "$PANELS/lin" --resources "$REPO/resources" --scale 1.0 \
      >/dev/null
    # The resource directory is passed explicitly rather than left to respath.cpp's module-relative
    # fallback, which resolves to nothing for a bare .exe sitting outside a bundle.
    wine "$BUILD/panelrender.exe" --out "$(winepath -w "$PANELS/win")" \
         --resources "$(winepath -w "$REPO/resources")" --scale 1.0 >/dev/null

    for _p in "${PEDALS[@]}"; do
      magick "$PANELS/lin/$_p.png" -depth 8 "rgb:$PANELS/lin-$_p.raw"
      magick "$PANELS/win/$_p.png" -depth 8 "rgb:$PANELS/win-$_p.raw"
    done

    PANELS="$PANELS" PANEL_MAX_PIXELS="$PANEL_MAX_PIXELS" \
    PANEL_MAX_DELTA="$PANEL_MAX_DELTA" PEDALS="${PEDALS[*]}" python3 - <<'PYEOF'
import os, sys

d          = os.environ["PANELS"]
max_pixels = int(os.environ["PANEL_MAX_PIXELS"])
max_delta  = int(os.environ["PANEL_MAX_DELTA"])

fail, tot_d, tot_p = [], 0, 0
for page in os.environ["PEDALS"].split():
    a = open(f"{d}/lin-{page}.raw", "rb").read()
    b = open(f"{d}/win-{page}.raw", "rb").read()
    if len(a) != len(b):
        fail.append(f"{page}: the two renders are different SIZES "
                    f"({len(a)//3} vs {len(b)//3} px) - the layout itself diverged")
        continue
    npx, ndiff, worst = len(a) // 3, 0, 0
    for i in range(0, len(a), 3):
        if a[i:i+3] != b[i:i+3]:
            ndiff += 1
            w = max(abs(x - y) for x, y in zip(a[i:i+3], b[i:i+3]))
            if w > worst:
                worst = w
    tot_d += ndiff
    tot_p += npx
    print(f"  {page:<9} {ndiff:>5} of {npx:>8} px differ, worst channel delta {worst}/255")
    if ndiff > max_pixels:
        fail.append(f"{page}: {ndiff} differing pixels, cap is {max_pixels}")
    if worst > max_delta:
        fail.append(f"{page}: worst channel delta {worst}/255, cap is {max_delta} - "
                    f"that is ink moving, not rounding")
print(f"  {'TOTAL':<9} {tot_d:>5} of {tot_p:>8} px")

if fail:
    print("\nthe Windows editor does not draw what the Linux one draws:", file=sys.stderr)
    for f in fail:
        print(f"  {f}", file=sys.stderr)
    print("\nCheck that the sysroot's cairo/freetype/pixman/libpng/zlib still match", file=sys.stderr)
    print("this machine's system versions - scripts/build-win-deps.sh pins them", file=sys.stderr)
    print("to exactly that, and this comparison is why.", file=sys.stderr)
    sys.exit(1)
PYEOF
  fi
fi

# --- licence, attribution and instructions ----------------------------------
cp "$REPO/NOTICE" "$REPO/LICENSE" "$REPO/README.md" "$PKGDIR/"

cat > "$PKGDIR/INSTALL.txt" <<EOF
Rations Pedals ${VERSION} - five VST3 stompboxes for Windows

    RationsBoost.vst3      Drive, Tone, Level          an overdrive, mono in
    RationsChorus.vst3     Rate, Depth, Mix
    RationsFlanger.vst3    Rate, Depth, Manual, Regen
    RationsDelay.vst3      Time, Feedback, Tone, Mix   + tempo sync, ping-pong
    RationsReverb.vst3     Decay, Tone, Pre, Mix

They are five SEPARATE plug-ins, not one multi-effect: install any subset, use
as many instances of each as you like, in any order.

This archive holds them twice over - an installer, and the same five bundles
loose so you can install them by hand instead:

    RationsPedals-install.exe   the installer; it asks which pedals you want
    Rations<Name>.vst3          folders, not files. Copy the WHOLE folder.

Install, the easy way
---------------------
Run RationsPedals-install.exe and tick the pedals you want.

It is not code-signed, so Windows SmartScreen will show a blue "Windows
protected your PC" box. Click "More info", then "Run anyway" - or install by
hand instead, below; the two put exactly the same folders in exactly the same
place.

Run it as an administrator and it installs for everyone, in
C:\\Program Files\\Common Files\\VST3. Run it normally and it installs just for
you, in %LOCALAPPDATA%\\Programs\\Common\\VST3. Either way it tells you which,
and you can change the folder.

Each pedal gets its OWN entry in "Apps & features", so you can remove one and
keep the other four. Removing the last one takes the licence files with it.

Install, by hand
----------------
A VST3 plug-in is installed by copying its folder to where hosts look. There
are two such places, and hosts search them in this order:

  1. Just for you - no administrator rights needed:

         %LOCALAPPDATA%\\Programs\\Common\\VST3\\

     Paste that into the Explorer address bar. If the VST3 folder is not there,
     create it.

  2. For every user on the machine - needs administrator rights:

         C:\\Program Files\\Common Files\\VST3\\

Copy the Rations<Name>.vst3 folders you want into one of them, then rescan
plug-ins in your DAW.

Do not rename anything inside a folder. Each bundle carries its own art and
fonts in Contents\\Resources, and the binary in Contents\\x86_64-win must keep
the name Rations<Name>.vst3 or no host will load it. No bundle reads another
one's files, so a pedal you did not install is a pedal nothing misses.

To uninstall a hand-installed copy, delete its folder.

Whichever way you install, make sure you only have ONE copy of each. Hosts scan
both folders, so a copy left in each shows up twice in the plug-in list. The
installer offers to remove the other one for you.

Using the pedals
----------------
Drag a knob up and down, hold Shift for a fine drag, or use the scroll wheel.
The value replaces the knob's name while you are turning it.

The big switch at the bottom is the pedal's own footswitch: it crossfades
rather than clicking, and a pedal switched off is genuinely out of circuit. The
lamp above it says whether the pedal is in.

The small bat switch to its left is the HOST's bypass - a separate parameter,
so your DAW's own bypass button and any automation written against it have
somewhere to land.

MIDI learn
----------
The strip under the enclosure has one row: press Learn, then send the message
you want from your foot controller, and that message toggles the footswitch.
Clear unbinds it. The binding is saved with your project.

A learned CC or Program Change answers on ANY MIDI channel, because both arrive
at a VST3 plug-in as parameter changes and the channel is already gone by then.
Only a learned NOTE can be pinned to one MIDI channel.

Channels
--------
Boost takes a MONO input - a Tube Screamer is one circuit - with a mono or
stereo output. The other four take mono or stereo in, mono or stereo out. A
mono input feeding a stereo output is heard from both speakers.

Requirements
------------
64-bit Windows and a VST3 host. Nothing else: cairo, FreeType, libpng, zlib and
the GCC runtime are all linked into each plug-in, so there is no redistributable
to install and nothing to put beside a binary.

There is no 32-bit build, and there are no standalone applications in this
archive: those are JACK applications, so they ship with the Linux release -
which is a separate download.

Licence
-------
MIT. See LICENSE, and NOTICE for third-party attribution - which matters more
for this build than for the Linux one, because each Windows plug-in statically
links cairo, pixman, FreeType, libpng and zlib and therefore redistributes them.
EOF

# --- the installer ----------------------------------------------------------
# Built LAST, from the staged tree, so the .exe carries exactly the bundles that are also loose in
# the ZIP - the same stripped binaries and the same moduleinfo.json. Building it from build-win
# instead would quietly ship unstripped, unverified copies the moment either step above changed.
if [ "${RPEDALS_SKIP_INSTALLER:-0}" = "1" ]; then
  echo
  echo "WARNING: RPEDALS_SKIP_INSTALLER=1 — the ZIP has no RationsPedals-install.exe." >&2
  echo
else
  # VIProductVersion wants exactly four components; project() gives three.
  VERSION4="$VERSION"
  while [ "$(printf '%s' "$VERSION4" | tr -cd '.' | wc -c)" -lt 3 ]; do
    VERSION4="$VERSION4.0"
  done

  echo "building RationsPedals-install.exe with $MAKENSIS"
  "$MAKENSIS" -V2 -NOCD \
    "-DVERSION=$VERSION" "-DVERSION4=$VERSION4" \
    "-DSTAGE_DIR=$PKGDIR" "-DDOC_DIR=$PKGDIR" \
    "-DOUTFILE=$PKGDIR/RationsPedals-install.exe" \
    "$REPO/installer/rations-pedals.nsi"

  if [ ! -s "$PKGDIR/RationsPedals-install.exe" ]; then
    echo "makensis produced no RationsPedals-install.exe" >&2
    exit 1
  fi
  # It must be a PE executable, not whatever else ended up at that path. file(1) is not guaranteed
  # to be installed, so check the magic directly.
  if [ "$(head -c2 "$PKGDIR/RationsPedals-install.exe")" != "MZ" ]; then
    echo "RationsPedals-install.exe is not a PE executable" >&2
    exit 1
  fi
fi

mkdir -p "$REPO/dist"
ZIP="$REPO/dist/RationsPedals-${VERSION}-windows-x86_64.zip"
rm -f "$ZIP"
# python3 rather than zip(1): zip is not installed everywhere and this needs no extra package. -c
# takes the directory and stores it with its own name at the archive root, which is what an
# extract-anywhere release wants.
( cd "$STAGEDIR" && python3 -m zipfile -c "$ZIP" "RationsPedals-${VERSION}" )

echo ""
echo "Packaged: $ZIP"
echo ""
du -h "$ZIP"
