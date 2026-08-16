# SidebandMaw

SidebandMaw is an EsionHsrahLatigid JUCE audio-effect plug-in for live SSB frequency shifting, ring modulation, and folded sideband feedback.

It is designed for aggressive metallic translation and unstable edge texture while keeping host-facing output finite and bounded. Silent input remains silent; non-silent input is guarded against collapse into DC rails, clipped constants, non-finite samples, or near-muted output at extreme settings.

## Identity

- Product: `SidebandMaw`
- Bundle ID: `jp.ehl.sidebandmaw`
- Manufacturer: `EsionHsrahLatigid`
- Manufacturer code: `EHL_`
- Plug-in code: `SbMw`
- Formats: VST3, AU on Apple, Standalone

## Controls

| Control | Default | Purpose |
| --- | ---: | --- |
| `Shift` | `240 Hz` | Equal-frequency translation amount for SSB mode. |
| `Mode` | `Shift` | `Shift`, `Ring`, or `Maw`. |
| `Feedback` | `0.18` | Bounded re-entry around the modulated path. |
| `Spread` | `0.50` | Stereo divergence and quadrature skew. |
| `Drive` | `0.25` | Fold intensity for the nonlinear branch. |
| `Tone` | `8000 Hz` | Feedback and output damping frequency. |
| `Mix` | `1.00` | Wet/dry balance. |
| `Output` | `0 dB` | Final trim before the safety ceiling. |

## Build

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel 2
ctest --preset engine-debug

cmake --preset plugin-release -DEHL_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build --preset plugin-release --parallel 2
ctest --preset plugin-release
```

Readable local artifacts are staged under `artifacts/plugin-release/<platform>/`.

## Safety

SidebandMaw enforces finite sample guards, bounded feedback, DC filtering, damping, and a final digital ceiling. These are host-protection and audibility guards, not SPL or hearing-safety guarantees. Keep monitoring levels low while auditioning extreme settings.

## Source Basis

- SSB shifting uses an analytic-signal/Hilbert-transform structure: an allpass Hilbert approximation creates a quadrature branch, then the real and quadrature branches are multiplied by cosine/sine carrier phases to translate one sideband.
- Ring mode is balanced multiplication against the carrier; the deterministic tests verify sum and difference sidebands from a 440 Hz input and 600 Hz carrier.
- The plug-in target, formats, bundle metadata, and APVTS state flow follow JUCE's primary CMake API and AudioProcessorValueTreeState documentation.
- UI identity, palette, chrome, and canonical short logo are imported from pinned `juce-ehl-design-module` commit `46ba72a5cb98d84a1333bbdf71182aac136d9893`.

Primary references:

- Edward Bedrosian, "The Analytic Signal Representation of Modulated Waveforms," Proceedings of the IRE, 1962. DOI: `10.1109/JRPROC.1962.288236`.
- Kwonhue Choi and Huaping Liu, "Hilbert Transform, Analytic Signal, and SSB Modulation," in Problem-Based Learning in Communication Systems Using MATLAB and Simulink, Wiley, 2016. DOI: `10.1002/9781119060239.ch11`.
- Tamara Smyth, "Ring Modulation," UC San Diego Music 270a, 2019: https://musicweb.ucsd.edu/~trsmyth/modulation/Ring_Modulation_cont.html
- JUCE CMake API: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- JUCE AudioProcessorValueTreeState tutorial: https://juce.com/tutorials/tutorial_audio_processor_value_tree_state/
