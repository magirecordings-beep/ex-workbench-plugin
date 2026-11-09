#include "Engine.h"
#include "MorphTables.h"
#include <algorithm>
#include <cmath>

namespace exw {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr int MAX_NOTES = 40, SLOTS = 72, SUB = 16;
constexpr int MIDI_AP[16] = { 20, 21, 32, 33, 34, 35, 36, 37, 40, 41, 42, 43, 44, 45, 46, 47 };
constexpr int AP_DEFAULT_CC[16] = { 21, 22, 23, 24, 25, 26, 27, 28, 70, 71, 72, 73, 91, 93, 82, 83 };
enum { DST_FINE = 47, DST_PITCH = 48, DST_FILT = 56, DST_Q = 57, DST_VOL = 64, DST_PAN = 65 };
enum { K_ENV, K_LFO, K_CTRL };
enum { F_BYPASS, F_LP2, F_LP4, F_LP6, F_HP2, F_HP4, F_BP, F_NOTCH, F_EQ, F_PHASER, F_FLANGER, F_MORPH = 100 };

float dstScale (int d) {
    switch (d) { case DST_PITCH: return 3600; case DST_FINE: return 100; case DST_FILT: return 10140; case DST_Q: return 100; case DST_VOL: return 72; case DST_PAN: return 1; default: return 0; }
}
int dstSlot (int d) { switch (d) { case DST_PITCH: return 0; case DST_FINE: return 1; case DST_FILT: return 2; case DST_Q: return 3; case DST_VOL: return 4; case DST_PAN: return 5; default: return -1; } }
bool isCtrlSrc (int s) {
    if (s >= 16 && s <= 19) return true;
    for (int a : MIDI_AP) if (a == s) return true;
    return false;
}
// envelope sources: which envelope (0 vol, 1 filter, 2 aux) and polarity (0 +, 1 ~, 2 <)
bool envSrc (int s, int& which, int& pol) {
    if (s >= 72 && s <= 74) { which = 0; pol = s - 72; return true; }
    if (s >= 80 && s <= 82) { which = 1; pol = s - 80; return true; }
    if (s >= 88 && s <= 90) { which = 2; pol = s - 88; return true; }
    return false;
}
bool lfoSrc (int s, int& which, bool& uni) {
    if (s == 96) { which = 0; uni = false; return true; } if (s == 97) { which = 0; uni = true; return true; }
    if (s == 104) { which = 1; uni = false; return true; } if (s == 105) { which = 1; uni = true; return true; }
    return false;
}
int emuFilterType (int code, int& morph) {
    morph = -1;
    switch (code) {
        case 127: return F_BYPASS; case 0: return F_LP4; case 1: return F_LP2; case 2: return F_LP6; case 8: return F_HP2; case 9: return F_HP4;
        case 16: case 17: return F_BP; case 18: return F_NOTCH; case 32: case 33: case 34: return F_EQ; case 64: case 65: return F_PHASER;
        case 66: morph = 5; return F_MORPH; case 72: return F_FLANGER; case 80: morph = 0; return F_MORPH; case 81: morph = 1; return F_MORPH;
        default: if (code >= 128) { morph = (code - 128) % NUM_MORPHS; return F_MORPH; } return F_LP2;
    }
}
float lerpf (float a, float b, float m) { return a + (b - a) * m; }
float clampF (float f) { return std::max (20.0f, std::min (20000.0f, f)); }
float clamp1 (float v) { return std::max (-1.0f, std::min (1.0f, v)); }
} // namespace

Settings::Settings() { for (int i = 0; i < 16; ++i) apCC[(size_t) i] = AP_DEFAULT_CC[i]; }

int Engine::apSrc (int i) { return MIDI_AP[std::max (0, std::min (15, i))]; }

Engine::Engine (Settings& s) : set (s) {
    voices.resize (SLOTS); susHeld.assign (128, false); susPending.assign (128, false);
    for (int k = 0; k < 8; ++k) { driftFrom[k] = 0; driftTo[k] = (float) (std::uniform_real_distribution<> (-1, 1) (rng)); }
}

void Engine::prepare (double rate) { sr = rate > 0 ? rate : 44100; master.prepare (sr); allNotesOff(); }

const Preset* Engine::cur() const { return bank && preset >= 0 && preset < (int) bank->presets.size() ? &bank->presets[(size_t) preset] : nullptr; }

// E-MU envelope rate (stored 0..1) to seconds
float Engine::rateSec (float x) const { x = std::max (0.0f, std::min (1.0f, x)); return 163.69f * (std::pow (2.0f, 11.0f * x) - 1.0f) / 2047.0f; }

void Engine::setBank (std::shared_ptr<const Bank> b) { allNotesOff(); for (auto& v : voices) v.active = false; bank = std::move (b); preset = -1; if (bank && ! bank->presets.empty()) setPreset (0); }

void Engine::setPreset (int i) {
    if (! bank || i < 0 || i >= (int) bank->presets.size()) return;
    preset = i; monoGroups.clear(); lastSolo.clear();
    auto& p = bank->presets[(size_t) i];
    for (int k = 0; k < 16; ++k) ctrlVal[MIDI_AP[k]] = p.hasIc ? p.ic[(size_t) k] / 127.0f : 0.0f;
    apChanged.store (0xffff);
}

void Engine::setApKnob (int i, float v) { if (i >= 0 && i < 16) ctrlVal[MIDI_AP[i]] = std::max (0.0f, std::min (1.0f, v)); }

int Engine::kbMode (const Voice* vd) const {
    int k = set.keys.load();
    if (k == 0) return vd ? vd->kb : 0;
    return k == 1 ? 0 : k == 2 ? 1 : 5;
}
float Engine::glideTime (const Voice* vd, int from, int to) const {
    if (from < 0 || from == to) return 0;
    int gm = set.glideMode.load();
    if (gm == 1) return 0;
    if (gm == 2) return set.glideMs.load() / 1000.0f;
    float g = vd ? vd->glide : 0; if (g <= 0) return 0;
    return vd->glideCurve ? g : g * (float) std::abs (to - from) / 12.0f;
}

float Engine::staticSource (int src, int note, int vel) {
    switch (src) {
        case 8: return note / 127.0f; case 9: return (note - 64) / 64.0f;
        case 10: return vel / 127.0f; case 11: return (vel - 64) / 64.0f; case 12: return vel / 127.0f - 1.0f;
        case 14: case 160: return 1.0f;
        case 4: case 100: case 101: return (float) std::uniform_real_distribution<> (-1, 1) (rng);
        default: return NAN;
    }
}

// the preset's modulation cords for this note: fixed offsets per destination plus live routes
void Engine::planMod (NoteVoice& nv, const Voice& v, int note, int vel, float fixed[8]) {
    for (int i = 0; i < 8; ++i) fixed[i] = 0;
    nv.routes.clear();
    const auto& cords = v.cords; size_t nC = cords.size();
    std::vector<float> amt (nC); std::vector<std::vector<std::pair<int, float>>> live (nC);
    for (size_t i = 0; i < nC; ++i) amt[i] = cords[i].amt;
    for (auto& c : cords) {
        if (c.dst < 168 || c.dst > 191) continue;
        size_t tgt = (size_t) (c.dst - 168); if (tgt >= nC) continue;
        float sv = staticSource (c.src, note, vel);
        if (! std::isnan (sv)) amt[tgt] += c.amt * sv; else if (isCtrlSrc (c.src)) live[tgt].push_back ({ c.src, c.amt });
    }
    bool own = set.bendOwn.load();
    for (size_t i = 0; i < nC; ++i) {
        const auto& c = cords[i]; float sc = dstScale (c.dst); int slot = dstSlot (c.dst);
        if (slot < 0) continue;
        if (c.src == 16 && (c.dst == DST_PITCH || c.dst == DST_FINE) && ! own) continue;
        float a = amt[i] / 100.0f;
        float sv = (c.dst == DST_Q && isCtrlSrc (c.src)) ? ctrlVal[c.src] : staticSource (c.src, note, vel);
        if (! std::isnan (sv)) {
            fixed[slot] += a * sv * sc;
            for (auto& x : live[i]) nv.routes.push_back ({ K_CTRL, 0, x.first, c.dst, x.second / 100.0f * sv * sc, {} });
            continue;
        }
        int which = 0, pol = 0; bool uniLfo = false;
        bool isEnv = envSrc (c.src, which, pol), isLfo = ! isEnv && lfoSrc (c.src, which, uniLfo), isCtl = ! isEnv && ! isLfo && isCtrlSrc (c.src);
        if (! isEnv && ! isLfo && ! isCtl) continue;
        float mul = 1, off = 0;
        if (isEnv) { if (pol == 1) { mul = 2; off = -1; } else if (pol == 2) off = -1; }
        if (isLfo && uniLfo) { mul = .5f; off = .5f; }
        fixed[slot] += a * off * sc;
        if (c.dst == DST_Q) continue; // resonance is set once per note
        Route r { isEnv ? K_ENV : isLfo ? K_LFO : K_CTRL, which, isCtl ? c.src : 0, c.dst, a * mul * sc, {} };
        for (auto& x : live[i]) r.att.push_back ({ x.first, x.second / 100.0f * mul * sc });
        if (r.gain == 0 && r.att.empty()) continue;
        if (isEnv) nv.menvUsed[which] = true; if (isLfo) nv.lfoUsed[which] = true;
        nv.routes.push_back (std::move (r));
    }
}

void Engine::computeStages (NoteVoice& nv, int code, float freq, float res100) {
    int morph = -1; int t = emuFilterType (code, morph);
    float fr = std::max (0.0f, std::min (1.0f, freq / 100.0f)), res = res100 / 100.0f, hz = 40.0f * std::pow (500.0f, fr);
    int n = 0; auto add = [&] (int ty, float f, float g, float q) { if (n < 6) nv.stages[n++] = { ty, f, g, q }; };
    switch (t) {
        case F_BYPASS: break;
        case F_LP2: case F_LP4: case F_LP6: case F_HP2: case F_HP4: {
            int cnt = t == F_LP2 || t == F_HP2 ? 1 : t == F_LP6 ? 3 : 2; bool lp = t == F_LP2 || t == F_LP4 || t == F_LP6;
            for (int i = 0; i < cnt; ++i) add (lp ? ST_LP : ST_HP, clampF (lp ? hz : hz / 2), 0, i ? -3.0f : -3.0f + res * 24.0f);
            break; }
        case F_BP: add (ST_BP, clampF (hz), 0, .7f + res * 14); add (ST_BP, clampF (hz), 0, .7f + res * 4); break;
        case F_NOTCH: add (ST_NO, clampF (hz), 0, .5f + res * 6); break;
        case F_EQ: add (ST_PK, clampF (hz), 6 + res * 12, 1.2f + res * 4); break;
        case F_PHASER: for (float k : { 1.0f, 1.7f, 2.9f, 4.6f }) add (ST_NO, clampF (hz * k / 2), 0, 1.5f + res * 6); break;
        case F_FLANGER: for (int k = 1; k <= 6; ++k) add (ST_NO, clampF (hz / 4 * (float) k), 0, 6 + res * 10); break;
        case F_MORPH: {
            const auto& M = MORPHS[morph < 0 ? 0 : morph];
            for (int i = 0; i < 6; ++i) {
                const auto& a = M.A[i]; const auto& b = M.B[i];
                float f = clampF (std::exp (lerpf (std::log (a.f), std::log (b.f), fr)));
                float g = a.hasG ? lerpf (a.g, b.hasG ? b.g : a.g, fr) * (a.t == ST_PK ? .55f + res * .9f : 1.0f) : 0.0f;
                float q;
                if (a.hasQ) q = (a.t == ST_LP || a.t == ST_HP) ? lerpf (a.q, b.hasQ ? b.q : a.q, fr) * (.4f + res * 1.2f) : lerpf (a.q, b.hasQ ? b.q : a.q, fr) * (.5f + res);
                else q = (a.t == ST_LP || a.t == ST_HP) ? -3.0f : 1.0f;
                add (a.t, f, g, q);
            }
            break; }
    }
    nv.nStages = n;
}

// Web Audio's BiquadFilterNode formulas (lowpass/highpass Q in dB), so filters match the browser version
void Engine::setBiquad (Biquad& b, const StageSpec& s, float cents) {
    // a resonant peak pushed near the top of the band rings at half the sample rate (heard as crackle): above
    // 12 kHz the resonance and any boost fade away
    const double f0 = s.f * std::pow (2.0, cents / 1200.0), f = std::max (5.0, std::min ({ sr * .45, 20000.0, f0 }));
    const double top = std::max (0.0, std::min (1.0, (std::min (f0, 22000.0) - 12000.0) / 8000.0));
    const float q = (s.t == ST_LP || s.t == ST_HP) ? (float) (s.q + (std::min (s.q, -3.0f) - s.q) * top) : s.q;
    const float gq = (s.t == ST_PK || s.t == ST_LS || s.t == ST_HS) ? (float) (s.g * (1 - top)) : s.g;
    double w0 = 2 * PI * f / sr, cw = std::cos (w0), sw = std::sin (w0), A = std::pow (10.0, gq / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (s.t) {
        case ST_LP: { double al = sw / (2 * std::pow (10.0, q / 20.0)); b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break; }
        case ST_HP: { double al = sw / (2 * std::pow (10.0, q / 20.0)); b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break; }
        case ST_BP: { double al = sw / (2 * std::max (1e-4f, s.q)); b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break; }
        case ST_NO: { double al = sw / (2 * std::max (1e-4f, s.q)); b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break; }
        case ST_AP: { double al = sw / (2 * std::max (1e-4f, s.q)); b0 = 1 - al; b1 = -2 * cw; b2 = 1 + al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break; }
        case ST_PK: { double al = sw / (2 * std::max (1e-4f, s.q)); b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break; }
        case ST_LS: { double al = sw / 2 * std::sqrt (2.0), sa = 2 * al * std::sqrt (A);
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; break; }
        case ST_HS: { double al = sw / 2 * std::sqrt (2.0), sa = 2 * al * std::sqrt (A);
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; break; }
        default: break;
    }
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
}

void Engine::stealIfNeeded() {
    int notes = 0; for (auto& v : voices) if (v.active && ! v.ended && v.stealFade < 0) ++notes;
    while (notes > MAX_NOTES) {
        NoteVoice* pick = nullptr;
        for (auto& v : voices) if (v.active && v.releasing && v.stealFade < 0 && (! pick || v.age < pick->age)) pick = &v;
        if (! pick) for (auto& v : voices) if (v.active && v.stealFade < 0 && (! pick || v.age < pick->age)) pick = &v;
        if (! pick) break;
        pick->stealFade = .01f; pick->releasing = true; --notes;
    }
}

void Engine::startNote (int note, int vel, const Voice& v, const Zone& z, int glideFrom, bool solo, int group) {
    const Sample* s = z.ref; if (! s || s->frames < 2) return;
    // a free slot, or the quietest/oldest one
    NoteVoice* nvp = nullptr;
    for (auto& x : voices) if (! x.active) { nvp = &x; break; }
    if (! nvp) { for (auto& x : voices) if (! nvp || (x.releasing && ! nvp->releasing) || (x.releasing == nvp->releasing && x.age < nvp->age)) nvp = &x; }
    NoteVoice& nv = *nvp;
    nv = NoteVoice();
    nv.active = true; nv.note = note; nv.startNote = note; nv.vel = vel; nv.age = ++ageCounter; nv.s = s; nv.vd = &v; nv.pre = cur(); nv.monoGroup = group;
    float semis = (float) (note + v.transpose - z.root + v.coarse) + (v.fine + z.fine + set.tuneCents.load()) / 100.0f;
    nv.baseRate = std::pow (2.0, semis / 12.0) * s->rate / sr;
    nv.baseGain = std::pow (10.0f, v.volDb / 20.0f); nv.basePan = v.pan / 64.0f;
    float fixed[8]; planMod (nv, v, note, vel, fixed);
    nv.fixedPitch = fixed[0] + fixed[1]; nv.fixedFilt = fixed[2]; nv.cordPan = fixed[5];
    nv.amp = std::pow (10.0f, fixed[4] / 20.0f);
    // amp envelope
    float es[6], el[6];
    if (! v.envs.empty()) for (int i = 0; i < 6; ++i) { es[i] = rateSec (v.envs[0].t[i]); el[i] = v.envs[0].l[i]; }
    else { const float ds[6] = { .002f, 0, .6f, 0, .35f, 0 }, dl[6] = { 100, 100, 100, 100, 0, 0 }; for (int i = 0; i < 6; ++i) { es[i] = ds[i]; el[i] = dl[i]; } }
    float tt = 0; nv.envPts[0][0] = 0; nv.envPts[0][1] = 0;
    for (int i = 0; i < 4; ++i) { tt += std::max (i ? 0.0f : .003f, es[i]); nv.envPts[i + 1][0] = tt; nv.envPts[i + 1][1] = nv.amp * std::max (0.0f, el[i]) / 100.0f; }
    nv.r1 = std::max (.008f, es[4]); nv.r2 = std::max (.008f, es[5]); nv.relL1 = nv.amp * std::max (0.0f, el[4]) / 100.0f;
    // modulation envelopes and LFOs
    for (int w = 0; w < 3; ++w) if (nv.menvUsed[w] && (size_t) w < v.envs.size()) {
        auto& me = nv.menv[w]; const auto& e = v.envs[(size_t) w]; float t2 = 0; me.n = 0; me.pts[0][0] = 0; me.pts[0][1] = 0;
        for (int i = 0; i < 4; ++i) { t2 += std::max (i ? 0.0f : .002f, rateSec (e.t[i])); me.pts[i + 1][0] = t2; me.pts[i + 1][1] = clamp1 (e.l[i] / 100.0f); }
        me.n = 5; me.r1 = std::max (.008f, rateSec (e.t[4])); me.r2 = std::max (.008f, rateSec (e.t[5])); me.l4 = clamp1 (e.l[4] / 100.0f); me.l5 = clamp1 (e.l[5] / 100.0f);
    } else nv.menvUsed[w] = false;
    for (int w = 0; w < 2; ++w) if (nv.lfoUsed[w] && (size_t) w < v.plfos.size()) {
        const auto& L = v.plfos[(size_t) w]; nv.lfo[w].rate = L.rate; nv.lfo[w].shape = L.shape == 16 ? 1 : 0; nv.lfo[w].delay = L.delay > 0 ? rateSec (L.delay) : 0;
    } else nv.lfoUsed[w] = false;
    // filter (resonance takes its cords once, at the start)
    if (v.hasFilter) {
        float res = std::round (v.fq * 100);
        int morph; if (emuFilterType (v.ftype, morph) != F_BYPASS && fixed[3] != 0) res = std::max (0.0f, std::min (100.0f, res + fixed[3]));
        { int mo; if (emuFilterType (v.ftype, mo) != F_BYPASS) res = std::max (0.0f, std::min (100.0f, res + set.resShift.load())); }
        computeStages (nv, v.ftype, std::round (v.ffreq * 100), res);
    }
    nv.filtMod = false; for (auto& r : nv.routes) if (r.dst == DST_FILT) nv.filtMod = true;
    nv.shiftNow = set.cutShiftOct.load() * 1200.0f;
    for (int i = 0; i < nv.nStages; ++i) setBiquad (nv.bq[i], nv.stages[i], nv.fixedFilt + nv.shiftNow);
    nv.lastFiltCents = nv.fixedFilt + nv.shiftNow;
    nv.panNow = std::max (-1.0f, std::min (1.0f, nv.basePan + nv.cordPan));
    // bend
    nv.ownBend = set.bendOwn.load() && v.bendUp >= 0;
    nv.bendCents = nv.ownBend ? v.bendUp * 100.0f : set.bendRange.load() * 100.0f;
    // glide: a cents offset sliding to 0
    float gt = glideFrom >= 0 ? glideTime (&v, glideFrom, note) : 0;
    nv.glideFrom = gt > 0 ? (glideFrom - note) * 100.0 : 0; nv.glideTo = 0; nv.glideT = 0; nv.glideDur = gt;
    (void) solo;
    // unison copies
    int n = 1; // unison isn't part of Xtreme Lab
    nv.copies.assign ((size_t) n, Copy());
    if (n > 1) {
        float det = set.uniDetune.load(), spread = set.uniSpread.load() / 100.0f, ow = set.uniBlend.load() / 100.0f;
        int centre = 0; std::vector<float> pos ((size_t) n);
        for (int k = 0; k < n; ++k) { pos[(size_t) k] = (float) k / (float) (n - 1) * 2 - 1; if (std::fabs (pos[(size_t) k]) < std::fabs (pos[(size_t) centre])) centre = k; }
        float norm = 1.0f / std::sqrt (1.0f + (float) (n - 1) * ow * ow);
        bool shortAttack = s->loop && s->loopEnd > s->loopStart && (double) s->loopStart / s->rate < .03;
        for (int k = 0; k < n; ++k) {
            auto& c = nv.copies[(size_t) k]; bool outer = k != centre;
            c.det = pos[(size_t) k] * det;
            float pan = (k % 2 ? -1.0f : 1.0f) * std::fabs (pos[(size_t) k]) * spread;
            float x = (pan + 1) / 2, g = (outer ? ow : 1.0f) * norm;
            c.gl = (float) std::cos (x * PI / 2) * g; c.gr = (float) std::sin (x * PI / 2) * g;
            c.drift = outer && set.uniDrift.load() > 0 ? k % 8 : -1;
            if (shortAttack && outer) c.pos = s->loopStart + std::uniform_real_distribution<> (0, 1) (rng) * (s->loopEnd + 1 - s->loopStart);
        }
    }
    nv.stereoPath = n > 1 || s->channels == 2;
    nv.kb = kbMode (&v);
    stealIfNeeded();
}

void Engine::releaseVoice (NoteVoice& nv) {
    if (! nv.active || nv.releasing) return;
    nv.releasing = true; nv.envRel = true; nv.relFrom = envLevel (nv); nv.relT = 0;
    for (auto& me : nv.menv) if (me.n) { me.rel = true; me.relFrom = me.v; me.relT = 0; }
}

float Engine::envLevel (const NoteVoice& nv) const {
    if (nv.envRel) {
        double t = nv.relT;
        if (t < nv.r1) return (float) (nv.relFrom + (nv.relL1 - nv.relFrom) * (t / nv.r1));
        t -= nv.r1; if (t < nv.r2) return (float) (nv.relL1 * (1 - t / nv.r2));
        return 0;
    }
    double t = nv.envT;
    for (int i = 1; i < 5; ++i) if (t < nv.envPts[i][0]) { double t0 = nv.envPts[i - 1][0], t1 = nv.envPts[i][0]; float a = nv.envPts[i - 1][1], b = nv.envPts[i][1]; return t1 > t0 ? (float) (a + (b - a) * (t - t0) / (t1 - t0)) : b; }
    return nv.envPts[4][1];
}

// ---------- notes ----------
void Engine::noteOn (int note, int vel) {
    if (note < 0 || note > 127) return;
    vel = std::max (1, std::min (127, vel));
    susHeld[(size_t) note] = true;
    if (susPending[(size_t) note]) { susPending[(size_t) note] = false; noteOffNow (note); }
    const Preset* p = cur(); if (! p) return;
    int prev = lastNote; lastNote = note;
    for (auto& v : p->voices) {
        if (note < v.key.lo || note > v.key.hi || vel < v.vel.lo || vel > v.vel.hi) continue;
        int k = note + v.transpose; const Zone* z = nullptr;
        for (auto& zz : v.zones) if (zz.ref && k >= zz.key.lo && k <= zz.key.hi && vel >= zz.vel.lo && vel <= zz.vel.hi) { z = &zz; break; }
        if (! z) continue;
        int m = kbMode (&v);
        if (m) monoOn (note, vel, v, *z, m);
        else startNote (note, vel, v, *z, glideTime (&v, prev, note) > 0 ? prev : -1, false, -1);
    }
}

void Engine::noteOff (int note) {
    if (note < 0 || note > 127) return;
    susHeld[(size_t) note] = false;
    if (sustain) { susPending[(size_t) note] = true; return; }
    noteOffNow (note);
}

void Engine::noteOffNow (int note) {
    monoOff (note);
    for (auto& v : voices) if (v.active && ! v.releasing && v.note == note) releaseVoice (v);
}

namespace { int prioOf (int m) { return m == 3 || m == 6 ? 1 : m == 4 || m == 7 ? 2 : 0; } }

void Engine::monoOn (int note, int vel, const Voice& v, const Zone& z, int m) {
    int gi = -1; for (size_t i = 0; i < monoGroups.size(); ++i) if (monoGroups[i].vd == &v) gi = (int) i;
    if (gi < 0) { monoGroups.push_back ({}); gi = (int) monoGroups.size() - 1; monoGroups.back().vd = &v; lastSolo.push_back (-1); }
    auto& g = monoGroups[(size_t) gi]; g.m = m;
    g.held.erase (std::remove_if (g.held.begin(), g.held.end(), [&] (auto& h) { return h.first == note; }), g.held.end());
    g.held.push_back ({ note, vel });
    NoteVoice* live = nullptr;
    for (auto& x : voices) if (x.active && ! x.releasing && ! x.ended && x.monoGroup == gi) live = &x;
    auto pick = [&] () -> std::pair<int, int> {
        int p = prioOf (m); auto best = g.held.back();
        if (p == 1) for (auto& h : g.held) { if (h.first < best.first) best = h; }
        if (p == 2) for (auto& h : g.held) { if (h.first > best.first) best = h; }
        return best;
    };
    if (live && prioOf (m) != 0 && pick().first != note) return;
    bool legato = false; if (live) for (auto& h : g.held) if (h.first == live->note) legato = true;
    if (live && legato && m != 1) {
        float gt = glideTime (&v, live->note, note);
        double nowOff = live->glideDur > 0 && live->glideT < live->glideDur ? live->glideFrom + (live->glideTo - live->glideFrom) * live->glideT / live->glideDur : live->glideTo;
        live->glideFrom = nowOff; live->glideTo = (note - live->startNote) * 100.0; live->glideT = 0; live->glideDur = gt;
        live->note = note; return;
    }
    if (live) releaseVoice (*live);
    int from = live ? live->note : lastSolo[(size_t) gi];
    int fg = m == 8 ? -1 : (glideTime (&v, from, note) > 0 ? from : -1);
    startNote (note, vel, v, z, fg, true, gi);
    lastSolo[(size_t) gi] = note;
}

void Engine::monoOff (int note) {
    for (size_t gi = 0; gi < monoGroups.size(); ++gi) {
        auto& g = monoGroups[gi];
        bool had = false; for (auto& h : g.held) if (h.first == note) had = true;
        if (! had) continue;
        g.held.erase (std::remove_if (g.held.begin(), g.held.end(), [&] (auto& h) { return h.first == note; }), g.held.end());
        NoteVoice* curV = nullptr;
        for (auto& x : voices) if (x.active && ! x.releasing && ! x.ended && x.monoGroup == (int) gi && x.note == note) curV = &x;
        if (! curV || g.m < 5 || g.held.empty()) continue;
        int p = prioOf (g.m); auto next = g.held.back();
        if (p == 1) for (auto& h : g.held) { if (h.first < next.first) next = h; }
        if (p == 2) for (auto& h : g.held) { if (h.first > next.first) next = h; }
        float gt = glideTime (g.vd, note, next.first);
        double nowOff = curV->glideDur > 0 && curV->glideT < curV->glideDur ? curV->glideFrom + (curV->glideTo - curV->glideFrom) * curV->glideT / curV->glideDur : curV->glideTo;
        curV->glideFrom = nowOff; curV->glideTo = (next.first - curV->startNote) * 100.0; curV->glideT = 0; curV->glideDur = gt;
        curV->note = next.first;
    }
}

void Engine::controller (int cc, int value) {
    float v = value / 127.0f;
    if (cc == 0) bankMsb = value; else if (cc == 32) bankLsb = value;
    for (int i = 0; i < 16; ++i) if (set.apCC[(size_t) i].load() == cc) { ctrlVal[MIDI_AP[i]] = v; apChanged.fetch_or (1 << i); }
    if (cc == 64) {
        bool down = value >= 64;
        if (! down && sustain) { sustain = false; for (int n = 0; n < 128; ++n) if (susPending[(size_t) n]) { susPending[(size_t) n] = false; if (! susHeld[(size_t) n]) noteOffNow (n); } }
        sustain = down;
    }
    else if (cc == 1) ctrlVal[17] = v;
    else if (cc == 4) ctrlVal[19] = v;
    else if (cc == 120 || cc == 123) allNotesOff();
}
void Engine::pitchBend (int v14) { ctrlVal[16] = std::max (-1.0f, std::min (1.0f, (v14 - 8192) / 8191.0f)); }
void Engine::channelPressure (int value) { ctrlVal[18] = value / 127.0f; }
void Engine::programChange (int pc) {
    if (! bank) return;
    int blk = bankLsb >= 0 ? bankLsb : bankMsb >= 0 ? bankMsb : 0;
    int idx = blk * 128 + pc;
    if (idx >= 0 && idx < (int) bank->presets.size()) { allNotesOff(); setPreset (idx); }
}
void Engine::allNotesOff() {
    sustain = false; std::fill (susPending.begin(), susPending.end(), false); std::fill (susHeld.begin(), susHeld.end(), false);
    for (auto& v : voices) if (v.active) releaseVoice (v);
    for (auto& g : monoGroups) g.held.clear();
}

int Engine::activeNotes() const { int n = 0; for (auto& v : voices) if (v.active && ! v.ended) ++n; return n; }
int Engine::activePlayers() const { int n = 0; for (auto& v : voices) if (v.active && ! v.ended) n += (int) v.copies.size(); return n; }

// ---------- audio ----------
void Engine::render (float* L, float* R, int n) {
    std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
    const double dt = 1.0 / sr;
    const float driftAmt = set.uniDrift.load() / 100.0f * 12.0f;
    for (int base = 0; base < n; base += SUB) {
        int m = std::min (SUB, n - base);
        double subDur = m * dt;
        // shared drift wanderers (smooth random, 0.25..1 Hz)
        for (int k = 0; k < 8; ++k) {
            driftPh[k] += subDur * (.25 + k * .11);
            if (driftPh[k] >= 1) { driftPh[k] -= 1; driftFrom[k] = driftTo[k]; driftTo[k] = (float) std::uniform_real_distribution<> (-1, 1) (rng); }
            drift[k] = driftFrom[k] + (driftTo[k] - driftFrom[k]) * (float) (.5 - .5 * std::cos (PI * driftPh[k]));
        }
        for (auto& nv : voices) {
            if (! nv.active) continue;
            // --- control-rate values at the start of this sub-block ---
            float envSig[3] = { 0, 0, 0 }, lfoSig[2] = { 0, 0 };
            for (int w = 0; w < 3; ++w) if (nv.menvUsed[w]) {
                auto& me = nv.menv[w]; float val;
                if (me.rel) {
                    double t = me.relT;
                    val = t < me.r1 ? me.relFrom + (me.l4 - me.relFrom) * (float) (t / me.r1) : (t - me.r1 < me.r2 ? me.l4 + (me.l5 - me.l4) * (float) ((t - me.r1) / me.r2) : me.l5);
                    me.relT += subDur;
                } else {
                    double t = me.t; val = me.pts[4][1];
                    for (int i = 1; i < 5; ++i) if (t < me.pts[i][0]) { double t0 = me.pts[i - 1][0], t1 = me.pts[i][0]; val = t1 > t0 ? me.pts[i - 1][1] + (me.pts[i][1] - me.pts[i - 1][1]) * (float) ((t - t0) / (t1 - t0)) : me.pts[i][1]; break; }
                    me.t += subDur;
                }
                me.v = val; envSig[w] = val;
            }
            for (int w = 0; w < 2; ++w) if (nv.lfoUsed[w]) {
                auto& lf = nv.lfo[w]; double ph = lf.ph - std::floor (lf.ph);
                float val = lf.shape == 1 ? (float) (ph < .25 ? 4 * ph : ph < .75 ? 2 - 4 * ph : 4 * ph - 4) : (float) std::sin (2 * PI * ph);
                if (lf.delay > 0) val *= (float) std::min (1.0, nv.envT / lf.delay);
                lfoSig[w] = val; lf.ph += lf.rate * subDur;
            }
            float pitch = nv.fixedPitch, filt = nv.fixedFilt, pan = nv.basePan + nv.cordPan, vol = 1;
            for (auto& r : nv.routes) {
                float sig = r.kind == K_ENV ? envSig[r.which] : r.kind == K_LFO ? lfoSig[r.which] : ctrlVal[r.ctrl];
                float g = r.gain; for (auto& a : r.att) g += ctrlVal[a.first] * a.second;
                float c = sig * g;
                if (r.dst == DST_PITCH || r.dst == DST_FINE) pitch += c; else if (r.dst == DST_FILT) filt += c; else if (r.dst == DST_PAN) pan += c; else if (r.dst == DST_VOL) vol += c / 72.0f;
            }
            // bend and glide
            pitch += ctrlVal[16] * (nv.ownBend ? nv.bendCents : set.bendRange.load() * 100.0f);
            if (nv.glideDur > 0 && nv.glideT < nv.glideDur) { pitch += (float) (nv.glideFrom + (nv.glideTo - nv.glideFrom) * nv.glideT / nv.glideDur); nv.glideT += subDur; }
            else pitch += (float) nv.glideTo;
            pan = std::max (-1.0f, std::min (1.0f, pan));
            { const float want = set.cutShiftOct.load() * 1200.0f; nv.shiftNow += (want - nv.shiftNow) * std::min (1.0f, (float) (subDur / .02)); }
            filt += nv.shiftNow;
            // the cutoff target for the end of this little block; fast sweeps are followed sample by sample
            const float filtFrom = nv.lastFiltCents;
            const bool filtFast = nv.nStages && std::fabs (filt - filtFrom) > 3.0f;
            if (! filtFast && nv.nStages && std::fabs (filt - nv.lastFiltCents) > .25f) { for (int i = 0; i < nv.nStages; ++i) setBiquad (nv.bq[i], nv.stages[i], filt); nv.lastFiltCents = filt; }
            // per-copy increments
            const Sample& s = *nv.s; const int nc = (int) nv.copies.size();
            double inc[8];
            for (int k = 0; k < nc; ++k) { float c = pitch + nv.copies[(size_t) k].det + (nv.copies[(size_t) k].drift >= 0 ? drift[nv.copies[(size_t) k].drift] * driftAmt : 0.0f); inc[k] = nv.baseRate * std::pow (2.0, c / 1200.0); }
            const float panFrom = nv.panNow; nv.panNow = pan;
            const bool loop = s.loop && s.loopEnd > s.loopStart; const double loopLen = loop ? (s.loopEnd + 1 - s.loopStart) : 0, loopEndP = s.loopEnd + 1;
            const bool st = s.channels == 2 && ! s.R.empty();
            for (int i = 0; i < m; ++i) {
                // read every copy (cubic), mixed to mono or stereo
                float ml = 0, mr = 0; bool alive = false;
                for (int k = 0; k < nc; ++k) {
                    auto& c = nv.copies[(size_t) k];
                    if (c.pos < 0) continue;
                    if (loop) { while (c.pos >= loopEndP) c.pos -= loopLen; }
                    else if (c.pos >= s.frames - 1) { c.pos = -1; continue; }
                    alive = true;
                    int ip = (int) c.pos; float f = (float) (c.pos - ip);
                    auto at = [&] (const std::vector<int16_t>& d, int j) -> float {
                        if (loop && j > s.loopEnd) j -= (int) loopLen;
                        j = std::max (0, std::min (s.frames - 1, j)); return d[(size_t) j] * (1.0f / 32768.0f);
                    };
                    auto cubic = [&] (const std::vector<int16_t>& d) {
                        float xm1 = at (d, ip - 1), x0 = at (d, ip), x1 = at (d, ip + 1), x2 = at (d, ip + 2);
                        float c1 = .5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - .5f * x2, c3 = .5f * (x2 - xm1) + 1.5f * (x0 - x1);
                        return ((c3 * f + c2) * f + c1) * f + x0;
                    };
                    float a = cubic (s.L), b = st ? cubic (s.R) : a;
                    if (nc > 1) {
                        if (! st) { ml += a * c.gl; mr += a * c.gr; }
                        else { // stereo sample through a stereo panner
                            float g = std::sqrt (c.gl * c.gl + c.gr * c.gr), p = (std::atan2 (c.gr, c.gl) / (float) (PI / 2)) * 2 - 1;
                            if (p <= 0) { float x = (p + 1) * (float) PI / 2; ml += (a + b * std::cos (x)) * g; mr += b * std::sin (x) * g; }
                            else { float x = p * (float) PI / 2; ml += a * std::cos (x) * g; mr += (b + a * std::sin (x)) * g; }
                        }
                    } else { ml = a; mr = b; }
                    c.pos += inc[k];
                }
                if (! alive && ! nv.releasing) { nv.releasing = true; nv.envRel = true; nv.relFrom = 0; }
                // filter
                double xl = ml, xr = mr;
                if (filtFast) { const float c = filtFrom + (filt - filtFrom) * (float) (i + 1) / (float) m; for (int sIdx = 0; sIdx < nv.nStages; ++sIdx) setBiquad (nv.bq[sIdx], nv.stages[sIdx], c); if (i == m - 1) nv.lastFiltCents = filt; }
                // direct form I: its memory is real past samples, so cutoff jumps stay smooth
                for (int sIdx = 0; sIdx < nv.nStages; ++sIdx) {
                    auto& q = nv.bq[sIdx];
                    double yl = q.b0 * xl + q.b1 * q.x1[0] + q.b2 * q.x2[0] - q.a1 * q.y1[0] - q.a2 * q.y2[0]; q.x2[0] = q.x1[0]; q.x1[0] = xl; q.y2[0] = q.y1[0]; q.y1[0] = yl; xl = yl;
                    if (nv.stereoPath) { double yr = q.b0 * xr + q.b1 * q.x1[1] + q.b2 * q.x2[1] - q.a1 * q.y1[1] - q.a2 * q.y2[1]; q.x2[1] = q.x1[1]; q.x1[1] = xr; q.y2[1] = q.y1[1]; q.y1[1] = yr; xr = yr; }
                }
                // envelope, level, cord volume
                float g = envLevel (nv) * nv.baseGain * vol;
                if (nv.stealFade >= 0) { g *= std::max (0.0f, nv.stealFade / .01f); nv.stealFade -= (float) dt; if (nv.stealFade <= 0) nv.ended = true; }
                if (nv.envRel) { nv.relT += dt; if (nv.relT > nv.r1 + nv.r2 + .05) nv.ended = true; } else nv.envT += dt;
                float p = panFrom + (pan - panFrom) * (float) (i + 1) / (float) m;
                float ol, orr;
                if (! nv.stereoPath) { float x = (p + 1) / 2 * (float) PI / 2; ol = (float) xl * std::cos (x); orr = (float) xl * std::sin (x); }
                else if (p <= 0) { float x = (p + 1) * (float) PI / 2; ol = (float) (xl + xr * std::cos (x)); orr = (float) xr * std::sin (x); }
                else { float x = p * (float) PI / 2; ol = (float) xl * std::cos (x); orr = (float) (xr + xl * std::sin (x)); }
                L[base + i] += ol * g; R[base + i] += orr * g;
                if (nv.ended) break;
            }
            if (nv.ended) nv.active = false;
        }
    }
    // output: the browser version's voice bus level -> Analog colour -> bus compressor -> master knob -> soft ceiling
    for (int i = 0; i < n; ++i) { L[i] *= masterGain; R[i] *= masterGain; }
    MasterFx::Params mp; mp.on = set.colOn.load(); mp.mono = set.colMono.load(); mp.warmth = set.colWarmth.load(); mp.weight = set.colWeight.load(); mp.sub = set.colSub.load(); mp.smooth = set.colSmooth.load();
    master.setParams (mp); master.process (L, R, n);
    float mg = std::pow (10.0f, set.masterDb.load() / 20.0f);
    auto ceil = [] (float x) { float a = std::fabs (x); const float knee = .8f, top = .985f; if (a <= knee) return x; return std::copysign (knee + (top - knee) * std::tanh ((a - knee) / (top - knee)), x); };
    for (int i = 0; i < n; ++i) { L[i] = ceil (L[i] * mg); R[i] = ceil (R[i] * mg); }
}

} // namespace exw
