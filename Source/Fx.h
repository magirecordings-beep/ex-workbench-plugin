// Xtreme Lab effects, ported from the browser version:
//   FX A  reverb: Classic / Room / Hall / Plate / Ambience - synthesized noise tails through a convolver
//   FX B  Chorus, Flanger (DJ beat flanger), Phaser (after PhaseMistress), Stereo delay (after Ableton's Delay),
//         Drive (after Decapitator) - as a send, or as an insert on everything
//   Sends A/B per voice (the engine fills the send buses), and the master EQ.
#pragma once
#include <JuceHeader.h>
#include "engine/Engine.h"

namespace xfx {

// ---- RBJ biquad (transposed direct form II, like the browser's worklets) ----
struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    BQ& set (char type, double f, double q, double db, double fs);   // 'l' lp, 'h' hp, 'p' peak, 'L' low shelf, 'H' high shelf
    BQ& flat() { b0 = 1; b1 = b2 = a1 = a2 = 0; return *this; }
    inline float run (float x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
};

// every effect parameter: id, name, range, default (also builds the host parameters)
struct ParamDef { const char* id; const char* name; float lo, hi, def, step, skewCentre; const char* unit; const char* choices; };
const std::vector<ParamDef>& paramDefs();

class FxRack : public exw::FxHook, private juce::Timer {
public:
    explicit FxRack (juce::AudioProcessorValueTreeState& params);
    ~FxRack() override;
    void prepare (double sampleRate, int maxBlock);
    void setTempo (double bpm) { if (bpm > 20 && bpm < 400) tempo.store ((float) bpm); }
    // exw::FxHook (audio thread)
    void sends (float* L, float* R, const float* aL, const float* aR, const float* bL, const float* bR, int n) override;
    void post (float* L, float* R, int n) override;
    float sendLevel (int bus) const override;   // 0 = A, 1 = B (0..1); B is 0 while FX B is an insert

private:
    juce::AudioProcessorValueTreeState& ps;
    double fs = 48000; int maxN = 512;
    std::atomic<float> tempo { 120 };
    float p (const char* id) const;
    int pi (const char* id) const { return (int) std::lround (p (id)); }
    bool pb (const char* id) const { return p (id) > .5f; }

    // FX A
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 2048 } };
    std::vector<float> preBuf[2]; int preW = 0;
    BQ aTone[2];
    juce::String irKey; std::atomic<bool> irReady { false };
    juce::AudioBuffer<float> tmpA;
    float aGain = 0; int aIdle = 0; std::atomic<int> irLen { 0 };
    void timerCallback() override;   // recalculates the reverb tail when its shape changes
    static juce::AudioBuffer<float> makeIR (double fs, const juce::var& o);
    juce::var irParams() const;

    // FX B
    juce::AudioBuffer<float> tmpB;
    int bType = -1;
    void runB (float* L, float* R, int n, bool insert);
    // chorus
    std::vector<float> chBuf[2]; int chW = 0; double chPh[2] {};
    // flanger
    std::vector<float> flBuf[2]; int flW = 0; double flPh = 0; float flHp[2] {}, flHpx[2] {};
    // phaser
    double phPh = 0; float phZ[2][24] {}, phFb[2] {}, phA[2] {}, phEnv = 0, phDrift = 0, phDv = 0; juce::Random rnd;
    // drive
    struct DriveCh { BQ lc1, lc2, pre[2], post[2], tl, th, hc1, hc2; float up[12] {}, dn[48] {}; float dc = 0, dcx = 0; };
    DriveCh dr[2]; float drG = 1, drM = 1, drO = 1, drPin = 1e-6f, drPout = 1e-6f, drComp = 1; juce::String drKey;
    void driveSetup();
    // delay
    struct Side { std::vector<float> buf; int w = 0; double time[2] { .375, .375 }, target = .375; float g[2] { 1, 0 }; float gStep = 0; int active = 0; double busy = 0; BQ hp, lp; float lastLp = 0, lastMix = 0; };
    Side dl[2]; double dlPh = 0, dlClock = 0; int dlCoefCount = 0;
    void delayTimes (double& tl, double& tr) const;

    // master EQ
    BQ eqL[2], eqM[2], eqH[2]; float eqKey[4] { 1e9f, 1e9f, 1e9f, 1e9f };

};

} // namespace xfx
