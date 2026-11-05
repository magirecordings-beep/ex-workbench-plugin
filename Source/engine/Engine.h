// EX Workbench sound engine: plays Emulator X presets the way the browser version does.
// Plain C++17, no JUCE, so it can be tested on its own. Not thread-safe by itself: the plugin calls
// everything that changes state from the audio thread (MIDI) or hands settings over via atomics.
#pragma once
#include "Bank.h"
#include "MasterFx.h"
#include <array>
#include <atomic>
#include <memory>
#include <random>
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
    Settings();
};

class Engine {
public:
    explicit Engine (Settings& s);
    void prepare (double sampleRate);
    void setBank (std::shared_ptr<const Bank> bank);   // audio thread
    void setPreset (int index);                           // audio thread; resets controllers to the preset's starts
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
    int activeNotes() const;
    int activePlayers() const;
    std::atomic<int> apChanged { 0 };                   // bit per knob moved by MIDI (UI reads and clears)

    static int apSrc (int i);                           // cord source number of MIDI A-P

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; double z1[2] {}, z2[2] {}; };
    struct StageSpec { int t; float f, g, q; };
    struct Route { int kind; int which; int ctrl; int dst; float gain; std::vector<std::pair<int, float>> att; };
    struct ModEnv { float pts[6][2]; int n = 0; double t = 0; float v = 0; bool rel = false; float relFrom = 0, r1 = 0, r2 = 0, l4 = 0, l5 = 0; double relT = 0; };
    struct VLfo { double ph = 0; float rate = 1; int shape = 0; float delay = 0; };
    struct Copy { double pos = 0; float det = 0, gl = 1, gr = 1; int drift = -1; };
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
        int nStages = 0; StageSpec stages[6] {}; Biquad bq[6]; bool filtMod = false; float lastFiltCents = 1e9f;
        // unison copies (1 = plain)
        std::vector<Copy> copies; bool stereoPath = false;
        float panNow = 0;
        int kb = 0; int monoGroup = -1;
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
