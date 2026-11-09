// Xtreme Lab sound engine: plays Emulator X presets the way the browser version does.
// Plain C++17, no JUCE, so it can be tested on its own. Not thread-safe by itself: the plugin calls
// everything that changes state from the audio thread (MIDI) or hands settings over via atomics.
#pragma once
#include "Bank.h"
#include "MasterFx.h"
#include <array>
#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace exw {

// user settings shared with the UI / host parameters (read by the audio thread every block)
struct Settings {
    std::atomic<int> keys { 0 };            // 0 preset, 1 poly, 2 mono, 3 legato
    std::atomic<int> glideMode { 0 };       // 0 preset, 1 off, 2 custom
    std::atomic<float> glideMs { 120 };
    std::atomic<bool> uniOn { false };
    std::atomic<int> uniVoices { 5 };
    std::atomic<float> uniDetune { 20 }, uniSpread { 70 }, uniBlend { 70 }, uniDrift { 15 };
    std::atomic<bool> bendOwn { true };     // presets use their own bend range
    std::atomic<float> bendRange { 2 };     // semitones, otherwise
    std::atomic<float> masterDb { 0 };
    std::atomic<float> tuneCents { 0 };
    std::array<std::atomic<int>, 16> apCC;  // CC number for MIDI A-P (-1 none)
    // Analog colour on the output
    std::atomic<bool> colOn { true }, colMono { true };
    std::atomic<float> colWarmth { 35 }, colWeight { 40 }, colSub { 25 }, colSmooth { 20 };
    // Filter panel on presets: shift every voice's filter (octaves, resonance %)
    std::atomic<float> cutShiftOct { 0 }, resShift { 0 };
    // amp envelope: 0 = the preset's own (times scaled by envTime), 1 = own ADSR
    std::atomic<int> envMode { 0 };
    std::atomic<float> envA { .002f }, envD { .6f }, envS { 100 }, envR { .35f }, envVel { .7f }, envTime { 1 };
    // three LFOs on every voice: shape 0 sine 1 triangle 2 square 3 saw up 4 saw down 5 random 6 smooth random
    struct Lfo { std::atomic<int> shape { 0 }; std::atomic<float> rate { 5 }, delay { 0 }; std::atomic<bool> free { false }; std::atomic<float> pitch { 0 }, filter { 0 }, vol { 0 }, pan { 0 }; };
    Lfo lfo[3];
    std::atomic<float> tempo { 120 };    // for synced function generators
    Settings();
};

// a function generator: a row of steps (-1..1) played in time, to pitch (semitones), filter, volume and pan
struct FgDef {
    float steps[64] {};
    int end = 8;
    bool sync = true; int div = 3;   // 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
    float rate = 6;                  // steps per second when not synced
    int dir = 0;                     // forward, reverse, pendulum, random, walk, once
    bool smooth = false, free = false;
    float pitch = 0, filter = 0, vol = 0, pan = 0;
    bool used() const { if (! pitch && ! filter && ! vol && ! pan) return false; for (int i = 0; i < end && i < 64; ++i) if (steps[i] != 0) return true; return false; }
};

// the effects live outside the engine (the plugin supplies them): the engine fills two send buses per block,
// FX returns are mixed in before the voice bus level, then post() runs on the scaled mix (insert FX, EQ)
struct FxHook {
    virtual ~FxHook() = default;
    virtual void sends (float* L, float* R, const float* aL, const float* aR, const float* bL, const float* bR, int n) = 0;
    virtual void post (float* L, float* R, int n) = 0;
    virtual float sendLevel (int bus) const = 0;   // 0/1: global send A/B (0..1); 2: 1 while FX B takes sends
};

// a user patch: up to four layers, each one of the bank's presets with its own level, pan, tuning and ranges
struct Layer {
    int preset = -1;                       // index into the bank's presets (-1 = empty)
    bool on = true, solo = false;
    float volDb = 0, pan = 0;              // pan -100..100
    int coarse = 0; float fine = 0;        // semitones, cents
    int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
    float sendA = -1, sendB = -1;          // % to the effects; -1 = use the global sends
};
struct Patch {
    bool active = false;                   // false: play the selected preset on its own
    std::string name;
    Layer layers[4];
};

class Engine {
public:
    explicit Engine (Settings& s);
    void prepare (double sampleRate);
    void setBank (std::shared_ptr<const Bank> bank);   // audio thread
    void setPreset (int index);                           // audio thread; resets controllers to the preset's starts
    void setPatch (const Patch& p, bool reset);
    void setFg (int i, const FgDef& f) { if (i >= 0 && i < 3) fgs[i] = f; }   // audio thread           // audio thread; reset = a new patch (notes stop), else a live edit
    bool patchActive() const { return patch.active; }
    int presetIndex() const { return preset; }

    // MIDI (audio thread)
    void noteOn (int note, int vel);
    void noteOff (int note);
    void controller (int cc, int value);
    void pitchBend (int value14);
    void channelPressure (int value);
    void programChange (int program);
    void allNotesOff();
    // knobs A-P (index 0..15), 0..1, e.g. from host automation
    void setApKnob (int i, float v);
    float apKnob (int i) const { return ctrlVal[apSrc (i)]; }

    void render (float* L, float* R, int n);           // adds nothing: overwrites L/R
    void setFxHook (FxHook* h) { fx = h; }
    int activeNotes() const;
    int activePlayers() const;
    std::atomic<int> apChanged { 0 };                   // bit per knob moved by MIDI (UI reads and clears)

    static int apSrc (int i);                           // cord source number of MIDI A-P

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {}; }; // direct form I
    struct StageSpec { int t; float f, g, q; };
    struct Route { int kind; int which; int ctrl; int dst; float gain; std::vector<std::pair<int, float>> att; };
    struct ModEnv { float pts[6][2]; int n = 0; double t = 0; float v = 0; bool rel = false; float relFrom = 0, r1 = 0, r2 = 0, l4 = 0, l5 = 0; double relT = 0; };
    struct VLfo { double ph = 0; float rate = 1; int shape = 0; float delay = 0; };
    struct Copy { double pos = 0; float det = 0, gl = 1, gr = 1; int drift = -1; };
    struct FgRun { int i = -1, d = 1; double t = 0; float from = 0, cur = 0; bool done = false; };
    struct NoteVoice {
        bool active = false, releasing = false, ended = false;
        int note = 60, startNote = 60, vel = 100; uint64_t age = 0;
        const Sample* s = nullptr; const Voice* vd = nullptr; const Preset* pre = nullptr;
        double baseRate = 1;          // frames per output sample at 0 cents
        float baseGain = 1, basePan = 0, amp = 1;
        // amp envelope: points (seconds, level) then release
        float envPts[5][2] {}; double envT = 0; float gainNow = 0; bool envRel = false; float relFrom = 0, relL1 = 0, r1 = 0, r2 = 0; double relT = 0; float stealFade = -1;
        // modulation
        float fixedPitch = 0, fixedFilt = 0, cordPan = 0;
        std::vector<Route> routes; ModEnv menv[3]; bool menvUsed[3] {}; VLfo lfo[2]; bool lfoUsed[2] {};
        float bendCents = 0; bool ownBend = false;
        double glideFrom = 0, glideTo = 0, glideT = 0, glideDur = 0;  // cents offset ramp
        // filter
        int nStages = 0; StageSpec stages[6] {}; Biquad bq[6]; bool filtMod = false; float lastFiltCents = 1e9f; float shiftNow = 0;
        // unison copies (1 = plain)
        std::vector<Copy> copies; bool stereoPath = false;
        float panNow = 0;
        int kb = 0; int monoGroup = -1;
        int layer = -1; float lyG = 1; float sa = 0, sb = 0;
        double ulfoPh[3] {}; float ulfoHold[3][2] {}; FgRun fg[3];   // the LFO and function generator runs of this note   // patch layer (-1 none) and its level, following the layer's volume live
    };
    struct MonoGroup { const Voice* vd = nullptr; std::vector<std::pair<int, int>> held; int cur = -1; int m = 0; };

    Settings& set;
    double sr = 44100;
    std::shared_ptr<const Bank> bank;
    int preset = -1;
    std::vector<NoteVoice> voices;
    std::vector<MonoGroup> monoGroups;
    float ctrlVal[256] {};
    int bankMsb = -1, bankLsb = -1;
    bool sustain = false; std::vector<bool> susHeld, susPending;
    int lastNote = -1; uint64_t ageCounter = 0;
    std::vector<int> lastSolo;
    std::mt19937 rng { 12345 };
    float drift[8] {}, driftFrom[8] {}, driftTo[8] {}; double driftPh[8] {};
    float masterGain = .24f;
    Patch patch;
    FxHook* fx = nullptr;
    FgDef fgs[3];
    FgRun fgFree[3]; double lfoFreePh[3] {}; float lfoFreeHold[3][2] {};
    std::mt19937 lfoRng { 99 };
    void fgAdvance (const FgDef& f, FgRun& r, double dt);
    double fgStepSec (const FgDef& f) const;
    std::vector<float> busA[2], busB[2];
    void renderChunk (float* L, float* R, int n);
    // the layer a note is being started for (set around startNote)
    const Preset* playing = nullptr; float lyGain = 1, lyPan = 0, lyCents = 0; int lyIdx = -1;
    float layerLevel (int i) const;
    void playPreset (const Preset& p, int note, int vel, int prev);
    MasterFx master;

    const Preset* cur() const;
    float rateSec (float x) const;
    void startNote (int note, int vel, const Voice& v, const Zone& z, int glideFrom, bool solo, int monoGroup);
    void releaseVoice (NoteVoice& nv);
    void noteOffNow (int note);
    void planMod (NoteVoice& nv, const Voice& v, int note, int vel, float fixed[8]);
    void computeStages (NoteVoice& nv, int type, float freq, float res);
    void setBiquad (Biquad& b, const StageSpec& s, float cents);
    float glideTime (const Voice* vd, int from, int to) const;
    int kbMode (const Voice* vd) const;
    void monoOn (int note, int vel, const Voice& v, const Zone& z, int m);
    void monoOff (int note);
    float staticSource (int src, int note, int vel);
    void stealIfNeeded();
    float envLevel (const NoteVoice& nv) const;
};

} // namespace exw
