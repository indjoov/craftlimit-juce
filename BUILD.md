# CraftLimit — Build Guide

## Prerequisites

### macOS
- Xcode 13+ (for AU + VST3)
- CMake 3.22+ (`brew install cmake`)
- JUCE 7.x

### Windows
- Visual Studio 2022 (Community is free)
- CMake 3.22+ (included with VS2022)
- JUCE 7.x

### Linux
- GCC 10+ or Clang 12+
- CMake 3.22+
- JUCE dependencies: `sudo apt install libasound2-dev libfreetype-dev libcurl4-openssl-dev`

---

## Step 1 — Get JUCE

Clone JUCE into the project root:

```bash
cd CraftLimit
git clone https://github.com/juce-framework/JUCE.git
```

Or if you have JUCE installed elsewhere, edit `CMakeLists.txt`:
```cmake
# Replace:
add_subdirectory(JUCE)
# With:
add_subdirectory(/path/to/your/JUCE JUCE)
```

---

## Step 2 — Configure

```bash
mkdir build && cd build

# macOS (Universal Binary for M1 + Intel)
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

# Windows
cmake .. -G "Visual Studio 17 2022" -A x64

# Linux (VST3 only — no AU on Linux)
cmake .. -DCMAKE_BUILD_TYPE=Release
```

---

## Step 3 — Build

```bash
# macOS / Linux
cmake --build . --config Release --parallel

# Windows
cmake --build . --config Release
```

---

## Step 4 — Install the plugin

### VST3 (Studio One, DaVinci Resolve)

**macOS:**
```bash
cp -r CraftLimit_artefacts/Release/VST3/CraftLimit.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
```

**Windows:**
```
Copy CraftLimit_artefacts\Release\VST3\CraftLimit.vst3
  →  C:\Program Files\Common Files\VST3\
```

**Linux:**
```bash
cp -r CraftLimit_artefacts/Release/VST3/CraftLimit.vst3 \
      ~/.vst3/
```

---

### AU (macOS only — GarageBand, Logic, Ableton on Mac)

```bash
cp -r CraftLimit_artefacts/Release/AU/CraftLimit.component \
      ~/Library/Audio/Plug-Ins/Components/
```

Then reset the AU cache:
```bash
killall -9 AudioComponentRegistrar
auval -a   # validates the AU
```

---

## Step 5 — Load in your DAW

### Studio One
1. Preferences → Locations → VST Plugins → Add the VST3 folder
2. Rescan plugins
3. Find "CraftLimit" under FX → Dynamics

### DaVinci Resolve (Fairlight page)
1. Open Fairlight → Effects Library
2. Plugins → VST (not AU — Resolve uses VST3 on Mac too)
3. CraftLimit appears under Dynamics

### Ableton Live (Mac)
1. Preferences → Plug-Ins → Use Audio Units: ON
2. Or: Add VST3 folder → Rescan
3. In browser: Plug-Ins → CraftLimit

---

## Latency Compensation

CraftLimit introduces latency equal to the **Lookahead** setting.
At 5ms and 48kHz this is 240 samples.

All DAWs listed above support **PDC (Plugin Delay Compensation)**,
which automatically compensates for this — no manual offset needed.

---

## Project Structure

```
CraftLimit/
├── CMakeLists.txt          ← Build system
├── JUCE/                   ← JUCE framework (git clone here)
├── BUILD.md                ← This file
└── Source/
    ├── LimiterDSP.h        ← DSP algorithm declarations
    ├── LimiterDSP.cpp      ← Core limiting engine
    ├── PluginProcessor.h   ← JUCE AudioProcessor header
    ├── PluginProcessor.cpp ← Parameter system + audio routing
    ├── PluginEditor.h      ← GUI header
    └── PluginEditor.cpp    ← GUI implementation + Look & Feel
```

---

## DSP Algorithm Notes

The limiter uses **lookahead peak detection** — the same fundamental
approach as FabFilter Pro-L2:

1. Incoming audio is written into a circular delay buffer
2. The algorithm scans ahead in the buffer to find the true peak
3. An exponential envelope follower computes the required gain reduction
4. The **hold phase** prevents premature gain recovery after a peak
5. The delayed audio is multiplied by the smoothed gain, then scaled to ceiling

**Algorithm presets** adjust attack/release multipliers and hold time:
- `Transparent` — unity multipliers, no hold, hard knee
- `Dynamic` — slower attack/release, short hold, soft knee
- `Aggressive` — very fast attack, short release
- `Surgical` — fastest attack, medium release, 5ms hold, soft knee
- `Bus` — balanced for mix bus, slight hold

**Soft knee** blends the gain curve near the threshold for a gentler onset.

---

## Extending the Plugin

Ideas for further development:
- **True oversampling** — use `juce::dsp::Oversampling` class
- **Inter-Sample Peak detection** — upsample 4x before peak detection
- **Stereo link control** — choose max/avg/mid-side linking
- **Attack shape** — linear vs exponential transient response
- **Release curve** — add a logarithmic "auto-release" mode like Pro-L2
- **Gain reduction history** — scrolling GR waveform display
