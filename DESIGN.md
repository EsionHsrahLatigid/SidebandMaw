# SidebandMaw Design

SidebandMaw uses the shared `juce-ehl-design-module` for the canonical short logo, palette, LookAndFeel, and editor chrome.

## UI Contract

- Compact 512 x 320 editor.
- Monochrome four-level palette only.
- One canonical short logo through the shared module.
- One functional sideband meter, one status strip, and two rows of controls.
- No decorative background, fake hardware, glow, color accent, or ornamental noise.

## Functional Display

The meter is a quantized activity grid fed by the DSP snapshot. It shows wet energy and feedback energy as operational state, not as decoration.

## Controls

Signal path controls are visible on one surface:

- `SHIFT`, `FDBK`, `SPREAD`, `MODE`
- `DRIVE`, `TONE`, `MIX`, `OUT`

## Source Basis

- Shared visual primitives: `modules/juce-ehl-design-module` at commit `46ba72a5cb98d84a1333bbdf71182aac136d9893`, including `EHL::JuceDesign`, `paintEditorChrome`, `paintShortLogo`, and the embedded `assets/logos/white/logo-short.svg` path contract.
- EHL UI rules: monochrome four-level palette, compact one-surface workflow, canonical short mark, quantized operational displays, and no chromatic accent or fake hardware.
- DSP display source: `SidebandMawCore::copySnapshot` publishes RMS, feedback energy, shift, mode, warning state, and quantized meter cells from the audio processor for editor rendering.
