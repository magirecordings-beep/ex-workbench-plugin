// EX Workbench: E-MU Emulator X bank (.exb) and SamplePool (.ebl) reader.
// Plain C++17 (no JUCE), ported from the browser version's parser so both read banks identically.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace exw {

struct Sample {
    std::string name;
    int rate = 44100, channels = 1, frames = 0;
    bool loop = false;
    int loopStart = 0, loopEnd = 0;       // frames, end inclusive
    std::vector<int16_t> L, R;            // R empty for mono
};

struct Env6 { float t[6] {}; float l[6] {}; };  // E-MU six-stage envelope: rate (0..1) and level (%)
struct Cord { int src = 0, dst = 0; float amt = 0; };
struct PLfo { float delay = 0, rate = 1; int shape = 0; };
struct Range { int lo = 0, hi = 127; };

struct Zone {
    int sample = 0, root = 60; float fine = 0;
    Range key, vel;
    const Sample* ref = nullptr;          // resolved against the SamplePool
};

struct Voice {
    Range key, vel;
    int transpose = 0, coarse = 0; float fine = 0;
    float volDb = 0; int pan = 0;
    bool hasFilter = false; int ftype = 0; float ffreq = 0, fq = 0;
    std::vector<Env6> envs;               // [0] volume, [1] filter, [2] aux
    std::vector<Zone> zones;
    std::vector<Cord> cords;
    std::vector<PLfo> plfos;
    float glide = 0; int glideCurve = 0, kb = 0;
    float bendUp = -1, bendDn = -1;       // -1: not stored
};

struct Preset {
    std::string name;
    std::vector<Voice> voices;
    bool hasIc = false;
    std::array<uint8_t, 16> ic {};        // initial positions of MIDI A-P
    std::array<std::string, 16> labels;   // their scribble-strip names
};

struct Bank {
    std::string path, name, error;
    std::vector<Preset> presets;
    std::map<int, std::string> slNames;   // sample list: index -> name
    std::vector<std::unique_ptr<Sample>> samples;
    int missingZones = 0;
};

// Loads an .exb and the SamplePool folder next to it (or any folder of .ebl files given as poolDir).
// progress gets 0..1. Returns nullptr-free bank; check bank->error.
std::shared_ptr<Bank> loadBank (const std::string& exbPath, const std::function<void (float)>& progress = {});

// Reads one .ebl / sample file; nullptr if it isn't one.
std::unique_ptr<Sample> loadEbl (const std::string& path);

std::string presetCategory (const std::string& name);  // "syn" from "syn:ORiON"

} // namespace exw
