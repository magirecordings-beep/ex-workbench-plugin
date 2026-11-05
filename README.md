# EX Workbench plugin

E-MU Emulator X bank player (AU / VST3 / standalone) built with JUCE 8, using the same sound engine as the
browser version of EX Workbench: presets, zones, envelopes, filters (incl. the morph filters), modulation cords,
LFOs, MIDI A-P knobs, mono/legato/glide, per-preset bend range, unison and the Analog output stage.

## Getting a build
Push this repository to GitHub. The workflow in `.github/workflows/build-mac.yml` builds a universal
(Apple Silicon + Intel) AU, VST3 and app on GitHub's Mac machines and attaches `EX-Workbench-mac.zip` to the run
(Actions tab > latest run > Artifacts). Unzip it and run `Install.command`.

## Building locally (optional)
Requires Xcode and CMake: `cmake -B build -G Xcode && cmake --build build --config Release`

## Layout
- `Source/engine` – bank reader and sound engine (plain C++17, no JUCE; testable on its own)
- `Source/PluginProcessor.*` – the plugin: parameters, state, MIDI, bank loading
- `Source/PluginEditor.*` + `ui/index.html` – the faceplate UI in a web view
- `tests/` – offline render test and a processor test (`exw_render_test`, `exw_proc_test` targets)

JUCE is used under its free personal licence (or AGPLv3); check JUCE's terms before distributing builds.
