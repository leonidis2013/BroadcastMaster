# BroadcastMaster

Master-bus broadcast audio suite for streams and live broadcasts: loudness and stereo metering, a gain-staged
12-stage processing chain ending in a true-peak limiter, a solo bus for monitoring, and presets.
Built with JUCE, VST3, Windows and macOS, stereo only.

**Version 0.2.0** (all three build stages are in this release)

## The signal chain

Every stage has an on/off switch and an output gain (+/-12 dB) with a mini level meter, for gain staging.
Click a stage to open its settings in the panel below the chain.

| # | Stage | What it does |
|---|-------|--------------|
| 1 | Input trim | Gain before everything else |
| 2 | Subsonic HPF | 4th-order high-pass, 10 to 60 Hz |
| 3 | Phase rotation | 1 to 4 all-pass stages that lower voice peaks without changing the tone |
| 4 | Tonal EQ | 6 bands (low shelf, 4 bells, high shelf) plus tilt. Each band acts on stereo, mid only or side only. Minimum-phase in Live mode, linear-phase in Broadcast mode. The curve is drawn over the spectrum |
| 5 | Saturation | Tube, tape or transformer, anti-aliased, with amount and mix |
| 6 | Exciter / bass | Air exciter above a chosen frequency, and a bass enhancer that adds harmonics of the low end |
| 7 | Multiband compressor | 4 bands on Linkwitz-Riley crossovers, per-band threshold, ratio, attack, release, makeup, gain-reduction bars and a solo button |
| 8 | Auto leveler | Slow gain rider toward a target LUFS with range limit, speed, a gate that freezes the gain in silence, and a speech/music detector (Auto, Speech or Music) |
| 9 | Stereo utilities | Width, mid and side gain, polarity flips, mono sum, lip-sync delay 0 to 500 ms |
| 10 | Soft clipper | Smooth tanh shoulder with an adjustable linear zone, anti-aliased |
| 11 | True-peak limiter | Look-ahead limiter working on the 4x oversampled peak. Its "gain" is the ceiling |
| 12 | Output trim | Cut-only, 0 to -24 dB, after the limiter |

Not included on purpose: dynamic EQ, gate, de-esser, noise suppression, bass mono, dither, reference loader.

## Metering

- **Loudness:** momentary, short-term and gated integrated LUFS (ITU-R BS.1770-4), loudness range (EBU Tech 3342) and
  true peak. Targets: YouTube, Twitch / Kick (typical), Spotify, Apple Music / Podcast, EBU R128, ATSC A/85,
  ARIB TR-B32, Custom.
- **Stereo:** correlation, balance, goniometer, mid/side levels, mono-sum loss.
- **Spectrum:** analyser (1/3, 1/6 or 1/12 octave, flat / A / C weighting), pink target curve, spectrogram.
- **Dynamics:** crest factor, PSR, PLR, loudness range, sample peak, gain-reduction history.
- **Input / Output switch** for every meter (buttons, or keys `I`, `O`, `Tab`), and **Compare (delta)**.

## Master controls

- **Presets:** five factory presets (Init, Talk / Podcast, Music Show, Mixed Show, Gentle Leveler + Safety Limiter)
  and your own. Saved presets are small `.bmpreset` files in your user data folder
  (Settings has an "Open presets folder" button). A preset covers the stage gains, switches and module settings.
  It does not touch the loudness target, limiter ceiling, leveler target, lip-sync delay or any global switch.
- **Loudness target link:** choosing a destination also sets the leveler target and the limiter ceiling
  (can be switched off in Settings).
- **Bypass with loudness matching:** the bypassed signal is time-aligned with the processed one and brought to the
  same loudness (about a one-second average), so a louder result does not just sound better. Key `B`.
- **Latency mode:**
  - *Live* - minimum-phase EQ and 1 ms limiter look-ahead: 71 samples (1.5 ms) at 48 kHz.
  - *Broadcast* - linear-phase EQ and 5 ms look-ahead: 1543 samples (32 ms) at 48 kHz.

  The plug-in reports its latency to the host. Switching stages on or off never changes it. The lip-sync delay is
  deliberate and is not reported.
- **Solo bus (second stereo output):** four kinds of source - multiband band solo, delta listen (processed minus input),
  input / output cue, limiter delta - with monitor A and B, an A/B switch (keys `1`, `2`), a level control and a solo
  on/off (key `S`). Enable the second output in a host that supports it (LiveProfessor does).
  When the host cannot route a second output (OBS cannot), **Solo to main** makes the solo signal replace the main
  output while solo is on. It is off by default and the window shows a warning when it is active. Never leave it on
  while you are on air.

## Using it in OBS and LiveProfessor

- **LiveProfessor:** load it as a VST3 plug-in on the master bus. The Solo output appears as a second stereo output.
- **OBS Studio:** OBS's built-in VST filter only loads VST 2.x plug-ins, not VST3 (see the
  [OBS knowledge base](https://obsproject.com/kb/vst-2-x-plugin-filter)). To use BroadcastMaster directly in OBS you
  need a VST3 host plug-in for OBS such as atkAudio Plugin Host (OBS 31.1 or later, per
  [Elgato's guide](https://www.elgato.com/explorer/news/streaming/how-to-use-vst3-plugins-in-obs-studio/)).
  The other option is to run it in LiveProfessor and send that mix into OBS through a virtual audio cable.

## Installing

Download the latest build from the [Releases page](https://github.com/leonidis2013/BroadcastMaster/releases/tag/latest)
("Latest build"). It has one zip per platform: `BroadcastMaster-Windows-VST3.zip` and `BroadcastMaster-macOS-VST3.zip`
(universal: Apple silicon and Intel).

### Windows

1. Unzip `BroadcastMaster-Windows-VST3.zip`.
2. Copy `BroadcastMaster.vst3` to `C:\Program Files\Common Files\VST3`.
3. Rescan plug-ins in your host.

### macOS

The macOS build is ad-hoc signed, not notarised. A file downloaded in a browser carries a quarantine flag, so
Gatekeeper will say the plug-in is damaged or from an unidentified developer, or the host will skip it. Remove the flag
once, in Terminal:

1. Unzip `BroadcastMaster-macOS-VST3.zip`.
2. Copy `BroadcastMaster.vst3` to `~/Library/Audio/Plug-Ins/VST3` (this user only) or `/Library/Audio/Plug-Ins/VST3`
   (all users).
3. Remove the quarantine flag from the installed copy:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/BroadcastMaster.vst3
   ```

   For the system-wide folder use `sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/BroadcastMaster.vst3`.
4. Restart your host and rescan plug-ins.

To check that the flag is gone, run `xattr ~/Library/Audio/Plug-Ins/VST3/BroadcastMaster.vst3`. It should not list
`com.apple.quarantine`. If macOS still blocks it, open System Settings > Privacy & Security and choose "Open Anyway"
for BroadcastMaster.

## Keys

`I` / `O` / `Tab` meter source, `B` bypass, `S` solo, `1` / `2` monitor A / B.

## Building

GitHub Actions builds Windows and macOS (universal arm64 + x86_64) VST3 files on every push; the latest
build is published on the [Releases page](https://github.com/leonidis2013/BroadcastMaster/releases/tag/latest) (see
Installing above for the macOS `xattr` step).

Local build (CMake 3.22 or newer; JUCE 8.0.8 is downloaded automatically):

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BroadcastMaster_VST3
```

## Tests

The metering engine, every DSP module and the whole processing chain are plain C++ with no JUCE dependency, so they
are tested with known signals:

```
g++ -std=c++17 -O2 -ISource/Meters Tests/MeterTests.cpp -o meter_tests && ./meter_tests
g++ -std=c++17 -O2 -ISource        Tests/DspTests.cpp   -o dsp_tests   && ./dsp_tests
g++ -std=c++17 -O2 -ISource        Tests/ChainTests.cpp -o chain_tests && ./chain_tests
```

They cover loudness against a calibrated -23 dBFS sine, gates and loudness range, K-weighting, true peak, filter
responses, linear-phase EQ latency and block-size independence, saturation harmonics, crossover flatness and the
compression curve, leveler convergence and gate, true-peak limiting against an independent 8x checker, fixed
latency in both modes, aligned bypass, loudness matching, every solo source, click-free switching, and a stress
run with every stage on. CI runs them (with address and undefined-behaviour sanitizers) before the plug-in builds.

## Known limits

- The speech/music detector in the leveler is a heuristic (share of low-energy 10 ms frames). Percussive music can
  look like speech, which is what the manual Speech / Music modes are for.
- The limiter keeps 0.3 dB of margin for its inter-sample estimate. Content right up against Nyquist (for example
  full-band white noise) can still overshoot the ceiling by a fraction of a dB; programme material up to about
  18 kHz stays under it in the tests.
- The loudness-matched bypass is a one-second average, so it needs a moment to settle after a big change.
- Platform loudness figures are the commonly published targets. Platforms change them, and Twitch and Kick
  publish no official target, so check your destination's current spec or use the Custom preset.
- JUCE is licensed under AGPLv3 or a commercial licence. That matters if you distribute BroadcastMaster to others.
