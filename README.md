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
