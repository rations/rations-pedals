#!/usr/bin/env bash
# Build Rations Pedals for 64-bit Linux and package the release tarball into dist/.
#
# TEN PRODUCTS, ONE TARBALL. Five plug-ins and the five JACK applications that host them:
#
#   Rations<Name>.vst3            the plug-in, for a DAW. Each links cairo, FreeType, fontconfig
#                                 and libX11 and NOTHING else — in particular none of them links
#                                 JACK, which is checked below rather than assumed.
#   rations-<name>-standalone     that pedal without a DAW: the same bundle, hosted on JACK in a
#                                 window of its own, with a MIDI port for the footswitch.
#
# The standalones are HOSTS — each loads its own .vst3 rather than containing a second copy of it
# — so a binary and its bundle are not independent of one another. That is deliberate: the editor
# finds its art through dladdr() relative to the loaded module, so a standalone that linked the
# plug-in in would resolve resources by a different route from the one every user's DAW uses.
#
# THE FIVE PLUG-INS, HOWEVER, ARE INDEPENDENT. Nothing here may make one require another. A user
# who wants only the Delay copies one bundle, and the gates below are largely about keeping that
# true — above all the export gate, because five siblings in one host process is the situation
# that punishes a shared symbol.
#
# WINDOWS IS A SEPARATE RELEASE; see scripts/makedist-windows.sh.
#
# WHAT THIS GATES ON. Everything here is measured on the built binaries — what they link, what
# they export, what is inside each bundle, and that each standalone runs far enough to print its
# usage. All of it works on any machine: no JACK server, no X display. The proofs that need a
# built tree rather than a packaged one (scripts/pedal-gate.sh, the SDK validator) are run
# separately and are not repeated here.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${RPEDALS_BUILD_DIR:-$REPO/build}"
ARCH="$(uname -m)"

# The five, in the order they are listed everywhere else in this project.
PEDALS=(boost chorus flanger delay reverb)
declare -A TARGET=([boost]=RationsBoost [chorus]=RationsChorus [flanger]=RationsFlanger \
                   [delay]=RationsDelay [reverb]=RationsReverb)

cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -S "$REPO"
cmake --build "$BUILD" --parallel "$(nproc)"

# The version out of the project() call, read rather than duplicated, and read the SAME way
# makedist-windows.sh reads it so the two releases cannot be tagged differently from one another.
#
# Matched against project( specifically, NOT against the first VERSION in the file:
# cmake_minimum_required(VERSION 3.25.0) is above it, and a looser expression tags the release
# 3.25.0 without saying anything.
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

# Everything sits FLAT in the package. The standalones look for their bundle beside themselves
# first, so a user who extracts the archive and runs one straight out of it gets the right pedal
# without installing anything.
for pedal in "${PEDALS[@]}"; do
  name="${TARGET[$pedal]}"

  # --- the plug-in ----------------------------------------------------------
  BUNDLE="$BUILD/VST3/Release/${name}.vst3"
  if [ ! -d "$BUNDLE" ]; then
    echo "VST3 bundle not found at $BUNDLE" >&2
    exit 1
  fi
  cp -r "$BUNDLE" "$PKGDIR/"
  PKGBUNDLE="$PKGDIR/${name}.vst3"

  # ONE ARCHITECTURE FOLDER, AND IT IS THE LINUX ONE.
  #
  # A VST3 bundle holds Contents/<arch>/ per platform, so a build tree that was once configured
  # with the MinGW toolchain and later re-configured natively keeps its Contents/x86_64-win
  # directory: CMake writes the new binary beside the old one instead of replacing it, and
  # `cp -r` then carries a Windows DLL into the Linux tarball. It loads nowhere and is pure
  # weight. This is the mirror of the prune in makedist-windows.sh, for the mirror-image mistake.
  for _arch in "$PKGBUNDLE/Contents"/*/; do
    _arch="${_arch%/}"
    _name="$(basename "$_arch")"
    case "$_name" in
      Resources | "${ARCH}-linux") ;;
      *)
        echo "warning: removing $_name/ from ${name}.vst3 — it is not a Linux architecture" >&2
        echo "  folder. '$BUILD' was configured for another platform at some point; run" >&2
        echo "  'rm -rf $BUILD' and re-run this script to stop seeing this." >&2
        rm -rf "$_arch"
        ;;
    esac
  done

  PLUGIN_SO="$PKGBUNDLE/Contents/${ARCH}-linux/${name}.so"
  if [ ! -f "$PLUGIN_SO" ]; then
    echo "no ${name}.so inside $PKGBUNDLE/Contents/${ARCH}-linux/" >&2
    echo "The bundle layout is wrong; no host can load this." >&2
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

  strip --strip-unneeded "$PLUGIN_SO"

  # --- gates on the plug-in -------------------------------------------------
  # THE PLUG-IN MUST NOT LINK THE AUDIO BACKEND. Only the standalone hosts JACK; a bundle that
  # linked it would refuse to load on every machine without libjack installed, which is most of
  # them, and would do it silently — the host simply reports no such plug-in.
  if ldd "$PLUGIN_SO" | grep -qi 'libjack'; then
    echo "${name}.vst3 links libjack. Only the standalone may." >&2
    ldd "$PLUGIN_SO" | grep -i jack >&2
    exit 1
  fi

  # THE EXPORT LIST IS EXACTLY THE THREE ENTRY POINTS.
  #
  # Stricter than the parent project's gate, which checks only that the three are PRESENT because
  # weak libstdc++ template instantiations legitimately survive -fvisibility=hidden and
  # --exclude-libs,ALL there. They survive here too — libstdc++ declares `namespace std` with
  # default visibility — and here that is not acceptable: five sibling plug-ins load into one host
  # process, and the first one loaded would win those definitions for the other four. That is what
  # cmake/plugin-exports.map exists for, so this gate is what proves the version script is doing
  # its job on every release.
  EXPORTS="$(nm -D --defined-only "$PLUGIN_SO" | awk '$2 == "T" || $2 == "W" { print $3 }' \
             | sort | tr '\n' ' ')"
  if [ "$EXPORTS" != "GetPluginFactory ModuleEntry ModuleExit " ]; then
    echo "${name}.vst3 does not export exactly the three VST3 entry points:" >&2
    echo "  got: $EXPORTS" >&2
    echo "Check the --version-script link option and cmake/plugin-exports.map." >&2
    exit 1
  fi

  # NO STB_GNU_UNIQUE SYMBOLS. A unique symbol makes glibc's loader refuse to unload the library
  # and binds it process-wide, so two plug-ins sharing one collide — and the failure is an abort
  # inside the host, not an error we could report. They appear when a static-local in an inline
  # function or template escapes the visibility settings.
  UNIQUE_SYMS="$(nm -D --defined-only "$PLUGIN_SO" | awk '$2 == "u" { print $3 }')"
  if [ -n "$UNIQUE_SYMS" ]; then
    echo "${name}.vst3 exports STB_GNU_UNIQUE symbols:" >&2
    echo "$UNIQUE_SYMS" | head -10 | sed 's/^/  /' >&2
    echo "A host loading a second one of these would abort." >&2
    exit 1
  fi

  # --- the standalone -------------------------------------------------------
  # A shipped component, so a build that skipped it (JACK development files absent) must not
  # quietly produce a half release.
  APP="$BUILD/rations-${pedal}-standalone"
  if [ ! -f "$APP" ]; then
    echo "rations-${pedal}-standalone was not built — install the JACK development files" >&2
    echo "(libjack-jackd2-dev, or libjack-dev) and re-run, or the release would ship the" >&2
    echo "plug-ins only." >&2
    exit 1
  fi
  cp "$APP" "$PKGDIR/"
  strip --strip-unneeded "$PKGDIR/rations-${pedal}-standalone"

  # ...AND THE STANDALONE MUST link it. A standalone that somehow came out without libjack cannot
  # make a sound, and would not say so until it ran.
  if ! ldd "$PKGDIR/rations-${pedal}-standalone" | grep -qi 'libjack'; then
    echo "rations-${pedal}-standalone does not link libjack; it could not open an audio device." >&2
    exit 1
  fi

  # IT RUNS, AND IT KNOWS WHICH PEDAL IT IS. --help touches the argument parser, the identity
  # strings and the settings-path logic and returns 0, which is as far as anything can be driven
  # without a display and a JACK server. Checking that the usage names the RIGHT bundle is what
  # would catch five binaries built from one source with the defines crossed.
  HELP="$("$PKGDIR/rations-${pedal}-standalone" --help 2>&1 || true)"
  if ! printf '%s' "$HELP" | grep -q "usage: rations-${pedal}-standalone"; then
    echo "rations-${pedal}-standalone --help did not print its own usage:" >&2
    printf '%s\n' "$HELP" | head -5 >&2
    exit 1
  fi
  if ! printf '%s' "$HELP" | grep -q "${name}.vst3"; then
    echo "rations-${pedal}-standalone does not look for ${name}.vst3; the build defines are" >&2
    echo "crossed." >&2
    exit 1
  fi
done

# --- the five together, in one process --------------------------------------
# THE GATE THE PER-BUNDLE CHECKS CANNOT BE. Everything above is asked of one bundle at a time, and
# so is the SDK validator; the failure this project is most exposed to — five siblings sharing a
# symbol or a static — only appears when more than one is loaded. tools/loadall loads all five at
# once, keeps them all loaded, and runs audio through each of them interleaved a block at a time.
# It is run against the STAGED bundles, which are the stripped ones that ship, rather than the
# build tree's.
#
# It is not a DAW and does not replace loading them in one; that stays a release gate a person
# runs. It is the part of it that runs on every release, here, with no display and no audio device.
if [ ! -x "$BUILD/loadall" ]; then
  echo "loadall was not built; it is the only check that loads all five at once." >&2
  exit 1
fi
echo "loading all five in one process"
LOADALL_ARGS=()
for pedal in "${PEDALS[@]}"; do
  LOADALL_ARGS+=("$PKGDIR/${TARGET[$pedal]}.vst3")
done
if ! "$BUILD/loadall" "${LOADALL_ARGS[@]}" | sed 's/^/  /'; then
  echo "the five bundles do not coexist in one process." >&2
  exit 1
fi

# --- licence, attribution, launchers ----------------------------------------
cp "$REPO/NOTICE" "$REPO/LICENSE" "$REPO/README.md" "$PKGDIR/"

mkdir -p "$PKGDIR/desktop"
for pedal in "${PEDALS[@]}"; do
  if [ ! -f "$REPO/packaging/icons/rations-${pedal}-256.png" ]; then
    echo "the ${pedal} application icons are missing — run gui/make_icons.sh" >&2
    exit 1
  fi
  cp "$REPO/packaging/rations-${pedal}.desktop" "$PKGDIR/desktop/"
  cp "$REPO"/packaging/icons/rations-"${pedal}"-*.png "$PKGDIR/desktop/"
done

# Desktop entries are a file format with a validator; use it when it is here rather than finding
# out from a launcher that silently ignores the entry.
if command -v desktop-file-validate >/dev/null 2>&1; then
  for _entry in "$PKGDIR"/desktop/*.desktop; do
    if ! desktop-file-validate "$_entry"; then
      echo "$(basename "$_entry") is not a valid desktop entry." >&2
      exit 1
    fi
  done
fi

cat > "$PKGDIR/install.sh" <<'EOF'
#!/usr/bin/env bash
# Install (or remove) Rations Pedals for the current user. Nothing here needs root, and nothing is
# installed outside your home directory.
#
#   Rations<Name>.vst3           -> ~/.vst3                                   (for your DAW)
#   rations-<name>-standalone    -> ~/.local/bin                              (on JACK)
#   rations-<name>.desktop       -> ~/.local/share/applications               (a menu entry)
#   rations-<name>-<size>.png    -> ~/.local/share/icons/hicolor/<size>/apps  (its icon)
#
# By default all five are installed. Name the ones you want instead:
#
#   ./install.sh delay reverb
#
# Each standalone LOADS its own plug-in rather than containing it, and ~/.vst3 is one of the
# places it looks, so installing the two together is what makes the menu entry work from anywhere.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VST3_DIR="$HOME/.vst3"
BIN_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
ICON_ROOT="$HOME/.local/share/icons/hicolor"
SIZES=(16 24 32 48 64 128 256)

ALL=(boost chorus flanger delay reverb)
declare -A BUNDLE=([boost]=RationsBoost [chorus]=RationsChorus [flanger]=RationsFlanger \
                   [delay]=RationsDelay [reverb]=RationsReverb)

UNINSTALL=0
WANT=()
for arg in "$@"; do
  case "$arg" in
    --uninstall) UNINSTALL=1 ;;
    -h|--help)
      echo "usage: ./install.sh [--uninstall] [pedal ...]"
      echo "  pedals: ${ALL[*]}   (default: all five)"
      exit 0 ;;
    *)
      if [ -z "${BUNDLE[$arg]:-}" ]; then
        echo "unknown pedal '$arg'; expected one of: ${ALL[*]}" >&2
        exit 2
      fi
      WANT+=("$arg") ;;
  esac
done
[ ${#WANT[@]} -eq 0 ] && WANT=("${ALL[@]}")

refresh() {
  command -v update-desktop-database >/dev/null && update-desktop-database "$APP_DIR" 2>/dev/null || true
  command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t "$ICON_ROOT" 2>/dev/null || true
}

if [ "$UNINSTALL" = 1 ]; then
  for p in "${WANT[@]}"; do
    rm -rf "$VST3_DIR/${BUNDLE[$p]}.vst3"
    rm -f "$BIN_DIR/rations-$p-standalone"
    rm -f "$APP_DIR/rations-$p.desktop"
    for SIZE in "${SIZES[@]}"; do
      rm -f "$ICON_ROOT/${SIZE}x${SIZE}/apps/rations-$p.png"
    done
    echo "removed $p"
  done
  refresh
  echo
  echo "Your settings are NOT removed: ~/.config/RationsPedals/ holds where each pedal's"
  echo "knobs were left. Delete it by hand if you want it gone."
  exit 0
fi

mkdir -p "$VST3_DIR" "$BIN_DIR" "$APP_DIR"
for p in "${WANT[@]}"; do
  b="${BUNDLE[$p]}"
  rm -rf "$VST3_DIR/$b.vst3"
  cp -r "$HERE/$b.vst3" "$VST3_DIR/"
  install -m 755 "$HERE/rations-$p-standalone" "$BIN_DIR/rations-$p-standalone"
  install -m 644 "$HERE/desktop/rations-$p.desktop" "$APP_DIR/rations-$p.desktop"
  for SIZE in "${SIZES[@]}"; do
    mkdir -p "$ICON_ROOT/${SIZE}x${SIZE}/apps"
    install -m 644 "$HERE/desktop/rations-$p-${SIZE}.png" \
                   "$ICON_ROOT/${SIZE}x${SIZE}/apps/rations-$p.png"
  done
  echo "installed $b.vst3 and rations-$p-standalone"
done
refresh

echo
echo "  plug-ins    $VST3_DIR"
echo "  standalones $BIN_DIR"
echo "  launchers   $APP_DIR"
echo
echo "Rescan plug-ins in your DAW to pick them up."
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) echo "Note: $BIN_DIR is not on your PATH, so the standalones will not be found by"
     echo "name from a shell. The menu entries work either way." ;;
esac
echo
echo "To remove them again:  ./install.sh --uninstall [pedal ...]"
EOF
chmod +x "$PKGDIR/install.sh"

cat > "$PKGDIR/INSTALL.txt" <<EOF
Rations Pedals ${VERSION} — five guitar stompboxes for Linux

Five separate plug-ins, and the same five as JACK applications:

    RationsBoost.vst3      rations-boost-standalone      Drive, Tone, Level
    RationsChorus.vst3     rations-chorus-standalone     Rate, Depth, Mix
    RationsFlanger.vst3    rations-flanger-standalone    Rate, Depth, Manual, Regen
    RationsDelay.vst3      rations-delay-standalone      Time, Repeats, Tone, Mix, Sync, Ping
    RationsReverb.vst3     rations-reverb-standalone     Decay, Tone, Pre, Mix

Each plug-in is independent: install one, install all five. Each standalone is
a HOST rather than a second copy — it loads its own .vst3 — so keep a binary
and its bundle together, or install both with the script below.

Install
-------
    ./install.sh                 all five
    ./install.sh delay reverb    only the ones you name

Everything goes under your home directory and nothing needs root:

    ~/.vst3/Rations<Name>.vst3               the plug-ins
    ~/.local/bin/rations-<name>-standalone    the standalones
    ~/.local/share/applications/              menu entries, with icons

Then rescan plug-ins in your DAW. To remove: ./install.sh --uninstall [pedal ...]

You can also run one where you extracted it, with no installation at all:

    ./rations-delay-standalone

and copy the bundles into ~/.vst3 by hand if you only want the plug-ins. A
standalone looks for its bundle in \$RPEDALS_VST3, then beside itself, then
~/.vst3, then /usr/local/lib/vst3 and /usr/lib/vst3, and you can also pass the
path to it:

    ./rations-delay-standalone /path/to/RationsDelay.vst3

The standalones
---------------
They are JACK applications. Start a JACK server first (qjackctl, or e.g.
"jackd -R -d alsa -r 48000 -p 256"), or run a PipeWire desktop, which provides
one. With no server a pedal still opens its window, so you can look at it, but
it makes no sound and says so.

Each registers four ports, named after its own bundle:

    RationsDelay:in        your guitar — mono, as a guitar is
    RationsDelay:out_l     \\ the pedal, in stereo
    RationsDelay:out_r     /
    RationsDelay:midi_in   a MIDI footswitch

The audio ports are connected to the first physical capture and playback ports
found. The MIDI port is left UNCONNECTED on purpose: which of your MIDI devices
is the footswitch is not something to guess at, and the wrong guess has a
keyboard stomping the pedal. Connect it in your patchbay.

Where the knobs were left, and what the footswitch is learned to, is written to

    ~/.config/RationsPedals/Rations<Name>.state

when you close it, and read back when you start it. One file per pedal. Run it
with --no-state to skip both ends and start at the defaults.

Using the pedals
----------------
Drag a knob up and down, hold Shift for a fine drag, or use the scroll wheel.
The value replaces the knob's name while you are turning it.

The big switch at the bottom is the pedal's own footswitch: it crossfades
rather than clicking, and a pedal switched off is genuinely out of circuit.
The lamp above it says whether the pedal is in.

The small bat switch to its left is the HOST's bypass — a separate parameter,
so your DAW's own bypass button and any automation written against it have
somewhere to land.

MIDI learn
----------
The strip under the enclosure has one row: press Learn, then send the message
you want from your foot controller, and that message toggles the footswitch.
Clear unbinds it. The binding is saved with your project, or in the standalone's
settings file.

A learned CC or Program Change answers on ANY MIDI channel, because both arrive
at a VST3 plug-in as parameter changes and the channel is already gone by then.
Only a learned NOTE can be pinned to one MIDI channel.

Channels
--------
Boost takes a MONO input — a Tube Screamer is one circuit — with a mono or
stereo output. The other four take mono or stereo in, mono or stereo out. A
mono input feeding a stereo output is heard from both speakers.

Requirements
------------
The plug-ins need cairo, freetype2, fontconfig and libX11, which a desktop
Linux install already has. They do NOT link JACK.

The standalones additionally need the JACK client library (libjack.so.0) and a
running JACK server. On Debian/Devuan/Ubuntu:

    sudo apt install jackd2          # pulls in libjack-jackd2-0

On a PipeWire desktop, "pipewire-jack" provides the same library and server.

Nothing here needs a -dev package; those are only for building from source.

There is no 32-bit build. The Windows release is a separate download and ships
the plug-ins only.

Licence
-------
MIT. See LICENSE, and NOTICE for third-party attribution.
EOF

# --- one modification time, and a deterministic archive ----------------------
# THE SAME FIX AS THE WINDOWS SCRIPT'S, for the same reason and found the same way: two runs of
# this script minutes apart produced tarballs that differed as whole files even where every member
# was byte-identical. Three separate things carry a clock or an environment into a .tar.gz and all
# three have to be told not to:
#
#   * every member's mtime, which is the moment cp staged it;
#   * the member ORDER, which is readdir order and is not stable;
#   * the owner and group names, which are whoever ran the script;
#   * and gzip's own header, which stores the timestamp of the file it compressed unless -n.
#
# The mtime is the commit this was built from -- deterministic and meaningful, rather than "now".
# A tree with no git falls back to a fixed constant, because "now" is the bug.
SOURCE_EPOCH="$(git -C "$REPO" show -s --format=%ct HEAD 2>/dev/null || true)"
[ -n "$SOURCE_EPOCH" ] || SOURCE_EPOCH=1000000000
find "$PKGDIR" -exec touch -h -d "@$SOURCE_EPOCH" {} +

mkdir -p "$REPO/dist"
TARBALL="$REPO/dist/RationsPedals-${VERSION}-linux-${ARCH}.tar.gz"
rm -f "$TARBALL"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_EPOCH" \
    -cf - -C "$STAGEDIR" "RationsPedals-${VERSION}" | gzip -n > "$TARBALL"

echo ""
echo "Packaged: $TARBALL"
echo ""
du -h "$TARBALL"
