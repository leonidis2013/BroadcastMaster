# Third-party software

BroadcastMaster (Copyright (C) 2026 Jerod Ulicki, AGPL-3.0-only) is built with the following. All DSP, metering and UI code in `Source/` is original to this project.

| Component | Use | License |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.8 | Plug-in framework, GUI, audio utilities (fetched at build time, not stored in this repo) | AGPLv3 (JUCE's open-source option) |
| Steinberg VST3 SDK | VST3 plug-in interface, bundled inside JUCE | Under the terms that ship with JUCE 8.0.8 (see the licence files inside the JUCE download)  |
| Inno Setup | Builds the Windows installer (build tool only, nothing is shipped from it) | Inno Setup License |
| `pkgbuild` / `productbuild` | Builds the macOS installer (Apple tools, build only) | Apple |

Algorithms are implemented from public descriptions: ITU-R BS.1770 / EBU R128 K-weighting and loudness gating, Linkwitz-Riley crossovers, and Kaiser-windowed sinc oversampling for true-peak detection.

VST is a trademark of Steinberg Media Technologies GmbH.
