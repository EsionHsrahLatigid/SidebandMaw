# SidebandMaw Build

SidebandMaw is a JUCE/CMake audio effect.

## Local build

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel 2
ctest --preset engine-debug

cmake --preset plugin-release -DEHL_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build --preset plugin-release --parallel 2
ctest --preset plugin-release
```

When JUCE is already available locally, avoid network fetches:

```sh
cmake --preset plugin-release \
  -DEHL_JUCE_SOURCE_DIR=/path/to/JUCE \
  -DEHL_COPY_PLUGIN_AFTER_BUILD=OFF
```

Readable staged products are written under `artifacts/plugin-release/<platform>/`.
