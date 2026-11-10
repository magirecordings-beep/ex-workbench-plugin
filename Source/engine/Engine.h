// Xtreme Lab sound engine: plays Emulator X presets the way the browser version does.
// Plain C++17, no JUCE, so it can be tested on its own. Not thread-safe by itself: the plugin calls
// everything that changes state from the audio thread (MIDI) or hands settings over via atomics.
#pragma once
#include "Bank.h"
#include "MasterFx.h"
#include "Program.h"
#include <array>
#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace exw {

// user settings shared with the host parameters (read by the audio thread every block)
struct Settings {
    std::atomic<int> keys { 0 };            // 0 preset, 1 poly, 2 mono, 3 legato
    std::atomic<int> glideMode { 0 };       // 0 preset, 1 off, 2 custom
    std::atomic<float> glideMs { 120 };
    std::atomic<float> masterDb { 0 };
    std::array<std::atomic<int>, 16> apCC;  // CC number for MIDI A-P (-1 none)
    // Analog colour on the output
    std::atomic<bool> colOn { true }, colMono { true };
    std::atomic<float> colWarmth { 35 }, colWeight { 40 }, colSub { 25 }, colSmooth { 20 };
    std::atomic<float> tempo { 120 };    // for synced function generators
    Settings();
};

// the effects live outside the engine (the plugin supplies them): the engine fills two send buses per block,
// FX returns are mixed in before the voice bus level, then post() runs on the scaled mix (insert FX, EQ)
struct FxHook {
    virtual ~FxHook() = default;
    virtual void sends (float* L, float* R, const float* aL, const float* aR, const float* bL, const float* bR, int n) = 0;
    virtual void post (float* L, float* R, int n) = 0;
    virtual float sendLevel (int bus) const = 0;   // 2: 1 while FX B takes sends (it isn't an insert)
};

class Engine {
public:
    explicit Engine (Settings& s);
    void prepare (double sampleRate);
    void setBank (std::shared_ptr<const Bank> bank);   // audio thread
    // audio thread. The caller keeps the program alive until a later one has been taken.
    // reset: a new sound (notes stop, mono groups clear); ic: where the A-P knobs start (null = leave them)
    void setProgram (const Program* p, bool reset, const uint8_t* ic);
    const Program* program() const { return prog; }

    // MIDI (audio thread)
    void noteOn (int note, int vel);
    void noteOff (int note);
    void controller (int cc, int value);
    void pitchBend (int value14);
    void channelPressure (int value);
    int programChange (int program) const;   // the bank preset index a program change asks for (-1 none)
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
        int nStages = 0; StageSpec stages[6] {}; Biquad bq[6]; bool filtMod = false; float lastFiltCents = 1e9f;
        // the stage settings glide (filter envelope decay, live knob moves), in the browser worklet's way
        StageSpec stFrom[6] {}, stTo[6] {}; bool gliding = false, glideExp = false; double stK = 1, stKStep = 0, stTc = .01; int stDelay = 0;
        int filtRule = 0; float filtQ = 0; int progVer = -1; float presetCents = 0;   // 0 own preset filter, 1 preset + shift, 2 panel filter, 3 layer filter
        // unison copies (1 = plain)
        std::vector<Copy> copies; bool stereoPath = false;
        float panNow = 0;
        int kb = 0; int monoGroup = -1;
        int slot = -1; bool layer = false; float lyG = 1; float sa = 0, sb = 0;   // which slot of the program, its level (follows live)
        bool pv = false, loop = false; float liveCents0 = 0, liveCents = 0, lyPan = 0, lyGT = 1, saT = 0, sbT = 0;
        LfoSpec ulfo[3]; FgDef ufg[3];         // preset modulation in use; tuning when the note started
        double ulfoPh[3] {}; float ulfoHold[3][2] {}; FgRun fg[3];   // the LFO and function generator runs of this note
    };
    struct MonoGroup { const Voice* vd = nullptr; int slot = -1; std::vector<std::pair<int, int>> held; int cur = -1; int m = 0; };

    Settings& set;
    double sr = 44100;
    std::shared_ptr<const Bank> bank;
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
    const Program* prog = nullptr;
    FxHook* fx = nullptr;
    FgRun fgFree[5][3]; double lfoFreePh[5][3] {}; float lfoFreeHold[5][3][2] {}; double vibPh = 0;
    // patch master filter
    StageSpec mfSt[6] {}; int mfN = 0; Biquad mfBq[6]; double mfPh = 0; int mfVer = -1; float mfLast = 1e9f;
    std::mt19937 lfoRng { 99 };
    void fgAdvance (const FgDef& f, FgRun& r, double dt);
    double fgStepSec (const FgDef& f) const;
    std::vector<float> busA[2], busB[2];
    void renderChunk (float* L, float* R, int n);
    // the slot a note is being started for (set around startNote)
    int slotIdx = -1;
    const Slot* slotOf (const NoteVoice& nv) const { return prog && nv.slot >= 0 && nv.slot < prog->nSlots ? &prog->slots[nv.slot] : nullptr; }
    float slotLevel (int i) const;
    float slotCents (const Slot& s) const { return (s.layer ? s.coarse * 100.0f + s.fine : 0.0f) + (prog ? prog->tune : 0.0f); }
    void playSlot (int i, int note, int vel, int prev);
    const LfoSpec* lfosFor (const NoteVoice& nv) const;
    const FgDef* fgsFor (const NoteVoice& nv) const;
    bool baseSpec (const NoteVoice& nv, FiltSpec& out) const;   // false: no filter
    void filterStages (const FiltSpec& sp, float mod, StageSpec out[6], int& n) const;
    void voiceFilter (NoteVoice& nv, bool start);
    MasterFx master;

    float rateSec (float x) const;
    void startNote (int note, int vel, const Voice& v, const Zone& z, int glideFrom, bool solo, int monoGroup);
    void releaseVoice (NoteVoice& nv);
    void noteOffNow (int note);
    void planMod (NoteVoice& nv, const Voice& v, int note, int vel, float fixed[8]);

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
