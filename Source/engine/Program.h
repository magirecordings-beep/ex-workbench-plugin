// What the engine plays, as set up in the Xtreme Lab panel (the same model as the browser version):
// one source (a preset, an instrument = a family of samples, or a single sample) or a patch of up to four layers,
// each layer with its own filter, amp envelope, LFOs, function generators, sends, level, pan, tuning and ranges.
// Built on the message thread from the panel's JSON; the audio thread only reads it.
#pragma once
#include "Bank.h"

namespace exw {

// filter types of the Filter card ("lp2" ...; morph = index into MORPHS)
enum { F_BYPASS, F_LP2, F_LP4, F_LP6, F_HP2, F_HP4, F_BP, F_NOTCH, F_EQ, F_PHASER, F_FLANGER, F_MORPH = 100 };

struct FiltSpec { int type = F_BYPASS, morph = -1; float freq = 70, res = 20, env = 0, decay = .8f; };
struct EnvSpec { float s[6] { .002f, 0, .6f, 0, .35f, 0 }, l[6] { 100, 100, 100, 100, 0, 0 }; };   // seconds, levels %
struct LfoSpec { int shape = 0; float rate = 5, delay = 0; bool free = false; float pitch = 0, filter = 0, vol = 0, pan = 0;
                 bool used() const { return pitch || filter || vol || pan; } };
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

struct Slot {
    const Preset* preset = nullptr;   // what it plays (an instrument or sample is a one-voice stand-in preset)
    bool isPreset = false;            // a real bank preset: its own envelopes, cords and filters can be used
    bool layer = false;               // a patch layer (else: the selection on its own)
    bool on = true, solo = false;
    float volDb = 0, pan = 0;         // pan -100..100
    int coarse = 0; float fine = 0;
    Range key { 0, 127 }, vel { 1, 127 };
    bool useSrcEnv = true; EnvSpec env;
    bool filtFromPreset = true; FiltSpec filt;
    LfoSpec lfo[3]; FgDef fg[3];
    float sendA = 20, sendB = 0;      // %, layers only
};

struct Program {
    int version = 0;
    bool patch = false;
    Slot slots[4]; int nSlots = 0;
    // the panel's own cards when no patch is playing
    FiltSpec filt; bool usePreset = true; float ofsFreq = 0, ofsRes = 0, ofsEnv = 0, ofsDecay = .8f;
    bool envFromPreset = true; EnvSpec env;
    LfoSpec lfo[3]; FgDef fg[3];
    // Setup card
    float tune = 0, bend = 2, velsens = .7f, envscale = 1; bool bendp = true, loop = true, pmod = true;
    // patch master filter (after every layer), with its own sine LFO
    FiltSpec mf; float mfRate = .2f, mfDepth = 0;
    float sendA = 20, sendB = 0;      // the global sends (%)
};

} // namespace exw
