# Beetroot Tempura

A granular canvas firmware for the M-VAVE FM-1, modelled on the workflow of
Beetlecrab Tempera. Built on M-VAVE's official FM1 Open SDK (V14). It is an
independent implementation and shares no code or assets with Tempera.

## What it does

| Tempera | Beetroot Tempura on FM-1 |
|---|---|
| 8 tracks × 8 cells canvas | 8 tracks (the FM-1's 8 sample slots) × 8 cells, drawn on the 240×240 screen with live waveforms and grains |
| 4 emitters | 4 emitters (E1–E4), each placed on any set of cells |
| Grain rate, size, shape, spray X/Y, pitch, reverse | Rate 0.5–194 Hz, size 4 ms–2 s, 6 window shapes, spray X (track blend/jump), spray Y, position, scan, octave/semi/fine, pitch spray with quantise, reverse probability |
| Per-emitter ADSR, filter, LFO | ADSR, LP/BP/HP filter with env amount, LFO (6 shapes, 8 destinations) |
| Modulation, delay, reverb sends | Chorus/flanger/vibrato, tempo-syncable ping-pong delay, stereo reverb, per-emitter sends |
| 16 voices, poly/mono/latch | 16 voices, POLY / MONO (glide) / LATCH, sustain pedal |
| Live input track | Any track set to LIVE IN records Bluetooth audio from your phone (or USB audio) into a looping tape |
| Canvases | 8 canvases saved to Flash, named on the device |
| MIDI | USB, DIN and **BLE MIDI** in; key out over USB and BLE; per-emitter channels; cell triggering; CC map; program change; MIDI clock |

## Controls

| Control | Action |
|---|---|
| SELECT knob / MAIN knob | Move the cursor across tracks / rows |
| PAGE knob | Next page in the current section |
| K1–K4 | The four values shown on screen |
| REC | Place or remove the selected emitter on the cursor cell (hold REC and move to paint) |
| SEQ | Next emitter. Hold SEQ + first four white keys: pick E1–E4 |
| PLAY | Drone the selected emitter on/off |
| ARP (hold) | Play the cursor cell |
| Keys | PLAY mode: play the emitter's cells at that pitch. CELLS mode: 16 white keys = 2 rows of cells under the cursor |
| OCT− / OCT+ | Octave. Both together: switch PLAY ↔ CELLS |
| HOME, EDIT, ENV, LFO, FX, SCL, GLO, SAVE | Canvas, grain pages, envelope, filter/LFO, effects, track, settings (keyboard, MIDI, live input, Bluetooth, system), canvases |
| REC + PLAY | Freeze / unfreeze the live input |
| HOME + PLAY | All sound off |

If the encoders on your unit are in a different order than SELECT=E0,
PAGE=E1, K1–K4=E2–E5, MAIN=E6, swap them at the top of `handle_encoders()` in
`app/tempura/tempura_ui.c`.

## MIDI implementation

- **Notes:** MIDI IN = OMNI plays the selected emitter on any channel; CH 1-4 maps channels 1–4 to E1–E4.
- **Cells:** on the CELL channel (default 16), notes 36–99 trigger the 64 cells (C2 = track 1, top row; each next 8 notes = next track).
- **CC:** 1 LFO amount, 7 level, 10 pan, 20 rate, 21 size, 22 spray Y, 23 spray X, 24 position, 25 scan, 26 pitch spray, 27 reverse, 64 sustain, 71 resonance, 72 release, 73 attack, 74 cutoff, 91 reverb send, 93 mod send, 94 delay send, 120/123 all off.
- Pitch bend ±2 semitones; channel/poly pressure routed by the PRESSURE setting; program change 0–7 loads canvases 1–8; MIDI clock sets delay tempo.
- **BLE MIDI:** GLO → BLUETOOTH. Peripheral (default): connect from your phone's MIDI app. Central: REC scans, PLAY connects, ARP disconnects. The Bluetooth device name stays the FM-1's own name; the SDK does not expose it.

## Build (Windows)

1. Install JieLi toolchain **2.5.3** and set `FM1_PI32_TOOLCHAIN` to its `pi32` folder (usually `C:\JL\pi32`). See README.md for the download and checksum.
2. From this folder in PowerShell:

   ```powershell
   ./sdk/Build.ps1 -FirmwareOnly -Toolchain 'C:\JL\pi32'
   ```
3. The firmware is written to `build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc`. The build also checks SRAM, heap, BLE and USB linkage; a memory overflow fails at link time, before anything reaches the device.

## Flash

**With the Beetroot Tempura flasher (USB):**

1. Back up projects, the voice library and the sample bank in the M-VAVE workbench.
2. Open `tools/flasher/beetroot-tempura-flasher.html` in desktop Chrome or Edge
   (or Chrome on Android with a USB-OTG cable). Safari and iPhone have no Web MIDI.
   If the browser refuses MIDI from a local file, serve the folder with
   `python -m http.server` and open `http://localhost:8000/tools/flasher/beetroot-tempura-flasher.html`.
3. Choose `build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc` and `build\dx7\build-identity.json`.
4. Connect, tick the three checks, press Install. Allow "MIDI devices with system exclusive" when the browser asks.

The flasher runs M-VAVE's own updater code from `sdk/WebUpdate.js` unchanged:
it checks the package, flashes through the vendor loader and reads the build
back afterwards. Rebuild the page after an SDK update with
`python tools/flasher/make_flasher.py`.

**Or with the M-VAVE workbench:** https://open.m-vave.com/fm1-workbench.

To go back, flash FM1 Open or stock V15 from the M-VAVE workbench. The
firmware update path lives in the SDK and is untouched by this app.

## Flasher website (no files to upload)

`.github/workflows/flasher-site.yml` builds the firmware on a GitHub Windows
runner and publishes a flasher page with the firmware built in to GitHub
Pages, next to the simulator:

1. Create a GitHub repository and push this folder to its `main` branch.
2. In the repository: Settings → Pages → Source: **GitHub Actions**.
3. The workflow runs on every push (or from the Actions tab). When it
   finishes, open `https://<you>.github.io/<repo>/` in Chrome or Edge, or in
   Chrome on Android with a USB-OTG cable, connect the FM-1 and press Install.
   `/simulator.html` is the browser simulator.

The workflow downloads JieLi toolchain 2.5.3 from JieLi's server and
installs it unattended. If that step fails, build on a Windows PC instead
and run `python tools/flasher/make_flasher.py --firmware ... --identity ... --out site/index.html`
to make the same self-contained page.

## Samples

Tracks 1–8 use the FM-1 sample bank (about 19.5 s total, mono, 32 kHz).
Step 5 of the flasher site loads any 8 of 28 built-in sounds over USB, as
one of five packs (Original demo, Ambient, Plucked, Voices & air, Rhythm &
noise) or picked track by track, with a preview in the browser. All 28 are
synthesized from scratch: `tools/sounds/new_sounds.py` makes the newer 20,
and `tools/flasher/sound_library.json` holds all 28 encoded for the FM-1.
Your own WAVs load through the M-VAVE workbench sample manager. Any track
can instead be set to LIVE IN on the SCL page.

## Storage note

Canvas N is stored in the Flash sector FM1 Open uses for project slot N.
Saving canvas 3 replaces FM1 Open project 3. The sample bank, settings and
voice library are not touched.

## Memory and CPU

Larger buffers (grain cache, delay, reverb, live tape) are allocated after
boot while keeping a heap reserve for Bluetooth and USB; GLO → SYSTEM shows
what was obtained. A small built-in cache guarantees sound if the heap is
tight. The grain count adapts to measured CPU load (up to 48 grains).

## What to check on first boot

This build has been compiled and run in the browser simulator and checked
against the device headers on a 32-bit target, but not yet on hardware. On
first boot, open GLO → SYSTEM and note CPU %, grain limit and the memory
lines. If the CPU sits above ~80 %, lower `TP_MAX_GRAINS` in
`app/tempura/tempura.h`.

## Files

- `app/tempura/` — the instrument (engine, effects, UI, MIDI, storage)
- Hooks in `app/screen.c`, `app/midi_router.c`, `app/samples.c`, `app/audio_output.c`, `app/board/audio.c`, `web/engine/Simulator.c`

License: GPLv3 with the FM1 SDK linking exception, as for FM1 Open (see LICENSE.md, THIRD_PARTY.md).
