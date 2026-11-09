# Xtreme Lab plugin

E-MU Emulator X bank player (AU / VST3 / standalone) built with JUCE 8, using the same sound engine as the
browser version of Xtreme Lab: presets, zones, envelopes, filters (incl. the morph filters), modulation cords,
LFOs, MIDI A-P knobs, mono/legato/glide, per-preset bend range, filter cutoff/resonance shift and the Analog output stage.

## Getting a build
Every push runs `.github/workflows/build.yml` on GitHub's machines:
- macOS: universal (Apple Silicon + Intel) AU, VST3 and app -> `Xtreme-Lab-mac.zip` (run `Install.command`)
- Windows: 64-bit VST3 and app -> `Xtreme-Lab-windows.zip` (run `Install.bat`)

Download them from Actions tab > latest run > Artifacts, or from Releases for tags like `v0.2.0`.

## Building locally (optional)
Mac: Xcode and CMake: `cmake -B build -G Xcode && cmake --build build --config Release`
Windows: Visual Studio 2022, CMake, and the `Microsoft.Web.WebView2` NuGet package (see the workflow).

## Layout
- `Source/engine` – bank reader and sound engine (plain C++17, no JUCE; testable on its own)
- `Source/PluginProcessor.*` – the plugin: parameters, state, MIDI, bank loading
- `Source/PluginEditor.*` + `ui/index.html` – the faceplate UI in a web view
- `tests/` – offline render test and a processor test (`exw_render_test`, `exw_proc_test` targets)

JUCE is used under its free personal licence (or AGPLv3); check JUCE's terms before distributing builds.
