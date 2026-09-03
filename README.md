# Rations Pedals

Five guitar stompboxes as **separate VST3 plug-ins**, for **Linux** and **Windows**.

| | Pedal | Controls |
|---|---|---|
| `RationsBoost.vst3` | **Boost** — a Tube Screamer-style overdrive | Drive, Tone, Level |
| `RationsChorus.vst3` | **Chorus** — two modulated taps per channel | Rate, Depth, Mix |
| `RationsFlanger.vst3` | **Flanger** — swept comb with feedback | Rate, Depth, Manual, Regen |
| `RationsDelay.vst3` | **Delay** — tempo-syncable, optional ping-pong | Time, Feedback, Tone, Mix, Sync, Ping-Pong |
| `RationsReverb.vst3` | **Reverb** — Freeverb-lineage room | Decay, Tone, Pre-delay, Mix |

Each pedal is its own plug-in with its own enclosure, drawn at 1:1. Install one, install all
five; none of them needs any of the others.

They are built directly on the **VST 3 SDK** — no JUCE, no VSTGUI, no iPlug2. Each editor is a
`Steinberg::IPlugView` embedded in the host's own window and painted by hand with Cairo and
FreeType, so a bundle is a plug-in and a few PNGs and nothing else.

## What is on the panel

- **Knobs** — drag up and down, hold **Shift** for a fine drag, or use the scroll wheel. The
  value appears above the knob while you are turning it.
- **The footswitch** — the pedal's own bypass, the big switch at the bottom. It crossfades rather
  than clicking, and a pedal switched off is genuinely out of circuit: it resets and stops costing
  the audio thread anything.
- **The bat toggle** — the *host's* bypass parameter, so a DAW's own bypass button and any
  automation lane written against it have somewhere to land. It is a separate control from the
  footswitch on purpose.
- **The lamp** — lit when the pedal is in.
- **The MIDI strip**, under the enclosure — one MIDI-learn row, for switching the pedal on and
  off with a foot controller. Press **Learn**, then send the message you want (a CC, a note, or a
  program change); the row shows what it bound to, and **Clear** unbinds it. The binding is saved
  with the project. There is no settings window.

## Channels

Boost accepts a **mono input** — a Tube Screamer is one circuit and this is one instance of it —
with a mono or stereo output. The other four accept mono or stereo in, mono or stereo out. In
every case a mono input feeding a stereo output is heard from **both** speakers.

## Building

Requires CMake ≥ 3.25, a C++17 compiler, and Cairo + FreeType development packages. On Debian or
Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build \
                 libcairo2-dev libfreetype-dev libfontconfig-dev libx11-dev
```

Then:

```sh
git clone --recurse-submodules <this repository>
cd rations-pedals
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The five bundles land in `build/VST3/Release/`, and the five JACK applications in `build/`.
Install them by copying the ones you want to `~/.vst3/`:

```sh
mkdir -p ~/.vst3 && cp -r build/VST3/Release/Rations*.vst3 ~/.vst3/
```

or build a release tarball with an `install.sh` in it:

```sh
./scripts/makedist-linux.sh          # -> dist/RationsPedals-<version>-linux-x86_64.tar.gz
```

If you already have the SDK checked out somewhere, point the build at it with
`-DVST3_SDK_DIR=/path/to/vst3sdk` instead of using the submodule.

### Windows

Cross-compiled from Linux with MinGW-w64. `scripts/build-win-deps.sh` builds the static
dependency sysroot once, then:

```sh
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
cmake --build build-win -j"$(nproc)"
```

or build the release ZIP, which does all of that and then verifies it:

```sh
./scripts/makedist-windows.sh        # -> dist/RationsPedals-<version>-windows-x86_64.zip
```

The ZIP holds the five bundles loose and an installer that offers them as five components, each
with its own entry in Apps & features, so removing one leaves the other four alone. An unattended
install takes the whole set, or the subset you name:

```
RationsPedals-install.exe /S /DELAY /REVERB
```

Everything the script gates on is measured on the built binaries, and the last three run the
cross-built code under Wine: the import list (proving `-static` held), the export list (exactly
the three VST3 entry points), the SDK validator, the DSP compared against the native build sample
by sample, and the five faces compared against the native render pixel by pixel. **Wine is the
smoke test; a Windows machine is the gate.**

## Playing a pedal without a DAW

Each pedal is also a JACK application — the same bundle, hosted in a window of its own with a MIDI
port for a footswitch:

```sh
build/rations-delay-standalone
```

It registers `RationsDelay:in` (mono, as a guitar is), `RationsDelay:out_l` and `out_r`, connected
to the first physical ports it finds, plus `RationsDelay:midi_in`, which is left unconnected on
purpose. Where the knobs were left is remembered in `~/.config/RationsPedals/`; `--no-state` skips
that. These are built only when JACK's development headers are present, and never on Windows.

## Verifying a build

```sh
./scripts/pedal-gate.sh
```

runs two offline checks that need no host, no audio device and no display:

- **`pedalcheck`** — sweeps every control and asserts each pedal does what its panel says (Mix at
  zero passes the dry signal, Drive adds harmonics, the footswitch does not click, the delay lands
  on the time its knob shows, …). It also proves that **nothing allocates on the audio path**, and
  hashes each pedal's output against a golden value, so any change to the sound has to be
  deliberate. Those hashes are a **glibc** measurement — four of the five pedals reach `libm` on
  the audio path, and MinGW's is not glibc's — so on Windows the same tool is run with
  `--reference`, against a stream file the native build wrote with `--dump`, and compared sample
  by sample instead.
- **`panelrender`** — re-measures the enclosure art, renders every legend in the real font and
  fails if any of them would be clipped or would overlap another control.

And the SDK's own validator, over each bundle:

```sh
for p in Boost Chorus Flanger Delay Reverb; do
    build/bin/Release/validator "build/VST3/Release/Rations$p.vst3"
done
```

## Licence

MIT — see [LICENSE](LICENSE). Third-party components, the enclosure art and the bundled fonts are
covered by [NOTICE](NOTICE).

VST is a trademark of Steinberg Media Technologies GmbH.
