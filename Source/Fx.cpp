#include "Fx.h"

namespace xfx {

static constexpr double PI = 3.14159265358979323846;

BQ& BQ::set (char type, double f, double q, double db, double rate) {
    f = std::max (10.0, std::min (rate * .45, f));
    const double w = 2 * PI * f / rate, cw = std::cos (w), sw = std::sin (w), A = std::pow (10.0, db / 40), al = sw / (2 * q);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    if (type == 'l') { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; }
    else if (type == 'h') { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; }
    else if (type == 'p') { B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; }
    else if (type == 'L' || type == 'H') {
        const double s = std::sqrt (A) * 2 * al, sg = type == 'L' ? -1 : 1;
        B0 = A * ((A + 1) + sg * (A - 1) * cw + s); B1 = -2 * sg * A * ((A - 1) + sg * (A + 1) * cw); B2 = A * ((A + 1) + sg * (A - 1) * cw - s);
        A0 = (A + 1) - sg * (A - 1) * cw + s; A1 = 2 * sg * ((A - 1) - sg * (A + 1) * cw); A2 = (A + 1) - sg * (A - 1) * cw - s;
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    return *this;
}

const std::vector<ParamDef>& paramDefs() {
    static const std::vector<ParamDef> d = {
        { "sendA", "Send A (reverb)", 0, 100, 20, 1, 0, "%", nullptr },
        { "sendB", "Send B", 0, 100, 0, 1, 0, "%", nullptr },
        // FX A
        { "fxaOn", "FX A on", 0, 1, 1, 1, 0, "", "bool" },
        { "fxaType", "FX A type", 0, 4, 0, 1, 0, "", "Classic,Room,Hall,Plate,Ambience" },
        { "fxaSize", "Reverb size", 0, 100, 72, 1, 0, "%", nullptr },
        { "fxaDecay", "Reverb decay", .2f, 20, 2.4f, .1f, 3, "s", nullptr },
        { "fxaPre", "Reverb predelay", 0, 200, 20, 1, 0, "ms", nullptr },
        { "fxaDamp", "Reverb damping", 0, 100, 45, 1, 0, "%", nullptr },
        { "fxaWidth", "Reverb stereo", 0, 100, 100, 1, 0, "%", nullptr },
        { "fxaTone", "Reverb tone", 800, 16000, 7000, 10, 4000, "Hz", nullptr },
        { "fxaRet", "Reverb return", 0, 1, .8f, .01f, 0, "", nullptr },
        // FX B
        { "fxbOn", "FX B on", 0, 1, 1, 1, 0, "", "bool" },
        { "fxbType", "FX B type", 0, 4, 0, 1, 0, "", "Chorus,Flanger,Phaser,Stereo delay,Drive" },
        { "fxbIns", "FX B insert", 0, 1, 0, 1, 0, "", "bool" },
        { "fxbRet", "FX B return", 0, 1, .7f, .01f, 0, "", nullptr },
        { "chRate", "Chorus rate", .05f, 8, .6f, .01f, 1, "Hz", nullptr },
        { "chDepth", "Chorus depth", 0, 100, 45, 1, 0, "%", nullptr },
        // delay
        { "dlSync", "Delay sync", 0, 1, 1, 1, 0, "", "bool" },
        { "dlDivL", "Delay left note", 0, 7, 2, 1, 0, "", "1/16,1/8,3/16,1/4,5/16,3/8,1/2,1 bar" },
        { "dlDivR", "Delay right note", 0, 7, 3, 1, 0, "", "1/16,1/8,3/16,1/4,5/16,3/8,1/2,1 bar" },
        { "dlOffL", "Delay left offset", -33, 33, 0, 1, 0, "%", nullptr },
        { "dlOffR", "Delay right offset", -33, 33, 0, 1, 0, "%", nullptr },
        { "dlMsL", "Delay left time", 1, 2000, 375, 1, 400, "ms", nullptr },
        { "dlMsR", "Delay right time", 1, 2000, 500, 1, 400, "ms", nullptr },
        { "dlLink", "Delay link", 0, 1, 0, 1, 0, "", "bool" },
        { "dlPP", "Delay ping pong", 0, 1, 0, 1, 0, "", "bool" },
        { "dlFreeze", "Delay freeze", 0, 1, 0, 1, 0, "", "bool" },
        { "dlFb", "Delay feedback", 0, 95, 25, 1, 0, "%", nullptr },
        { "dlFilt", "Delay filter", 0, 1, 1, 1, 0, "", "bool" },
        { "dlFreq", "Delay filter freq", 50, 18000, 1200, 1, 1200, "Hz", nullptr },
        { "dlWidth", "Delay filter width", .5f, 9, 6, .1f, 0, "oct", nullptr },
        { "dlMRate", "Delay mod rate", .01f, 10, .5f, .01f, 1, "Hz", nullptr },
        { "dlMFilt", "Delay mod filter", 0, 100, 0, 1, 0, "%", nullptr },
        { "dlMTime", "Delay mod time", 0, 100, 0, 1, 0, "%", nullptr },
        { "dlMode", "Delay time change", 0, 2, 1, 1, 0, "", "Repitch,Fade,Jump" },
        // flanger
        { "flSync", "Flanger sync", 0, 1, 1, 1, 0, "", "bool" },
        { "flBeats", "Flanger sweep", 0, 7, 5, 1, 0, "", "1/2 beat,1 beat,2 beats,1 bar,2 bars,4 bars,8 bars,16 bars" },
        { "flRate", "Flanger rate", .02f, 10, .2f, .01f, 1, "Hz", nullptr },
        { "flDepth", "Flanger depth", 0, 100, 80, 1, 0, "%", nullptr },
        { "flFb", "Flanger feedback", -95, 95, 70, 1, 0, "%", nullptr },
        { "flSpread", "Flanger stereo", 0, 100, 20, 1, 0, "%", nullptr },
        { "flMix", "Flanger mix", 0, 1, .5f, .01f, 0, "", nullptr },
        // phaser
        { "phStages", "Phaser stages", 0, 5, 2, 1, 0, "", "4,6,8,12,16,24" },
        { "phFreq", "Phaser frequency", 50, 8000, 700, 1, 700, "Hz", nullptr },
        { "phDepth", "Phaser mod", 0, 100, 60, 1, 0, "%", nullptr },
        { "phShape", "Phaser shape", 0, 4, 0, 1, 0, "", "Sine,Tri,Square,Saw,Env" },
        { "phSync", "Phaser sync", 0, 1, 0, 1, 0, "", "bool" },
        { "phBeats", "Phaser beat", 0, 6, 4, 1, 0, "", "1/16,1/8,1/4,1/2,1 bar,2 bars,4 bars" },
        { "phRate", "Phaser rate", .02f, 10, .4f, .01f, 1, "Hz", nullptr },
        { "phRes", "Phaser resonance", -95, 95, 40, 1, 0, "%", nullptr },
        { "phWidth", "Phaser stereo", 0, 100, 30, 1, 0, "%", nullptr },
        { "phMix", "Phaser mix", 0, 1, .5f, .01f, 0, "", nullptr },
        { "phAnalog", "Phaser analog", 0, 1, 1, 1, 0, "", "bool" },
        // drive
        { "drStyle", "Drive style", 0, 4, 0, 1, 0, "", "A,E,N,T,P" },
        { "drDrive", "Drive", 0, 10, 3, .1f, 0, "", nullptr },
        { "drPunish", "Drive punish", 0, 1, 0, 1, 0, "", "bool" },
        { "drAuto", "Drive auto gain", 0, 1, 1, 1, 0, "", "bool" },
        { "drThump", "Drive thump", 0, 1, 0, 1, 0, "", "bool" },
        { "drSteep", "Drive steep", 0, 1, 0, 1, 0, "", "bool" },
        { "drLowcut", "Drive low cut", 10, 800, 10, 1, 100, "Hz", nullptr },
        { "drTone", "Drive tone", -1, 1, 0, .01f, 0, "", nullptr },
        { "drHighcut", "Drive high cut", 1500, 20000, 20000, 10, 6000, "Hz", nullptr },
        { "drMix", "Drive mix", 0, 1, 1, .01f, 0, "", nullptr },
        { "drOut", "Drive output", -24, 12, 0, .5f, 0, "dB", nullptr },
        // master EQ
        { "eqLow", "EQ low", -12, 12, 0, .5f, 0, "dB", nullptr },
        { "eqMid", "EQ mid", -12, 12, 0, .5f, 0, "dB", nullptr },
        { "eqMidHz", "EQ mid freq", 200, 8000, 1000, 1, 1000, "Hz", nullptr },
        { "eqHigh", "EQ high", -12, 12, 0, .5f, 0, "dB", nullptr },
    };
    return d;
}

FxRack::FxRack (juce::AudioProcessorValueTreeState& params) : ps (params) { startTimerHz (10); }
FxRack::~FxRack() { stopTimer(); }

float FxRack::p (const char* id) const { if (auto* v = ps.getRawParameterValue (id)) return v->load(); return 0; }

void FxRack::prepare (double rate, int block) {
    fs = rate > 0 ? rate : 48000; maxN = std::max (32, block);
    conv.prepare ({ fs, (juce::uint32) std::max (block, 4096), 2 });
    irKey = {}; irReady = false;
    tmpA.setSize (2, std::max (block, 4096)); tmpB.setSize (2, std::max (block, 4096));
    for (auto& b : preBuf) b.assign ((size_t) (fs * .5) + 8, 0.0f);
    preW = 0;
    for (auto& b : chBuf) b.assign (8192, 0.0f);
    for (auto& b : flBuf) b.assign (8192, 0.0f);
    for (auto& s : dl) { s.buf.assign ((size_t) (fs * 5.05) + 8, 0.0f); s.w = 0; s.g[0] = 1; s.g[1] = 0; s.active = 0; s.busy = 0; s.lastLp = s.lastMix = 0; s.hp.reset(); s.lp.reset(); }
    for (int c = 0; c < 2; ++c) { aTone[c].reset(); eqL[c].reset(); eqM[c].reset(); eqH[c].reset(); }
    for (auto& k : eqKey) k = 1e9f;
    drKey = {}; bType = -1;
    timerCallback(); // build the first reverb tail right away
}

float FxRack::sendLevel (int bus) const {
    if (bus == 2) return pb ("fxbIns") && pb ("fxbOn") ? 0.0f : 1.0f;
    if (bus == 1 && pb ("fxbIns") && pb ("fxbOn")) return 0;
    return p (bus == 0 ? "sendA" : "sendB") / 100.0f;
}

// ---------- FX A: synthesized reverb tails (same recipe as the browser version) ----------
juce::var FxRack::irParams() const {
    const float size = p ("fxaSize") / 100, damp = p ("fxaDamp") / 100, width = p ("fxaWidth") / 100, decay = p ("fxaDecay");
    auto o = new juce::DynamicObject(); juce::var v (o);
    auto set = [&] (double dc, double fc0, double end, double darken, double attack, int er, double erTime, double erAmp, double low, double w) {
        o->setProperty ("decay", dc); o->setProperty ("fc0", fc0); o->setProperty ("end", end); o->setProperty ("darken", darken); o->setProperty ("attack", attack);
        o->setProperty ("er", er); o->setProperty ("erTime", erTime); o->setProperty ("erAmp", erAmp); o->setProperty ("low", low); o->setProperty ("width", w);
    };
    switch (pi ("fxaType")) {
        case 0: set (.5 + std::pow (size, 1.6) * 8, 9500 - damp * 6500, .3, .5 + damp * 1.6, .012 + size * .035, 10, .012 + size * .045, .35, 70, width); break; // classic
        case 1: set (decay, 9000, .3, .9, .004, 12, .035, .5, 80, 1); break;   // room
        case 3: set (decay, 15000, .5, .45, .002, 0, 0, 0, 120, 1); break;     // plate
        case 4: set (decay, 9000, .3, 1.5, .002, 16, .02, .55, 100, 1); break; // ambience
        default: set (decay, 7500, .3, 1, .03, 8, .06, .35, 60, 1); break;     // hall
    }
    return v;
}

juce::AudioBuffer<float> FxRack::makeIR (double sr, const juce::var& o) {
    const double decay = o["decay"], attack = o["attack"], fc0 = o["fc0"], end = o["end"], darken = o["darken"];
    const int len = std::max (512, (int) std::floor (sr * std::min (24.0, decay * 1.25 + attack * 3 + .1)));
    juce::AudioBuffer<float> b (2, len);
    const double dec = 6.9 / std::max (.1, decay);
    uint32_t seed = 0x9e3779b9u ^ (uint32_t) std::lround (decay * 1000 + fc0);
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; };
    auto gauss = [&] { return (rnd() + rnd() + rnd() - 1.5) * 1.15; };
    std::vector<float> n1 ((size_t) len), n2 ((size_t) len);
    for (int i = 0; i < len; ++i) { n1[(size_t) i] = (float) gauss(); n2[(size_t) i] = (float) gauss(); }
    const double w = std::max (0.0, std::min (1.0, (double) o["width"])), hpA = std::exp (-2 * PI * (double) o["low"] / sr);
    const int er = o["er"]; const double erTime = o["erTime"], erAmp = o["erAmp"];
    for (int ch = 0; ch < 2; ++ch) {
        float* d = b.getWritePointer (ch); double l1 = 0, l2 = 0, hx = 0, hy = 0;
        for (int i = 0; i < len; ++i) {
            const double t = i / sr, fc = fc0 * (end + (1 - end) * std::exp (-t * darken)), a = 1 - std::exp (-2 * PI * fc / sr);
            const double x = ch ? n2[(size_t) i] * w + n1[(size_t) i] * (1 - w) : n1[(size_t) i];
            l1 += a * (x - l1); l2 += a * (l1 - l2);
            hy = hpA * (hy + l2 - hx); hx = l2;
            d[i] = (float) (hy * (1 - std::exp (-t / std::max (.001, attack))) * std::exp (-dec * t));
        }
        for (int r = 0; r < er; ++r) {
            const int at = (int) std::floor (sr * (erTime * (r + 1) / er) * (.7 + .6 * rnd())) + ch * (int) std::floor (sr * .0007 * (r % 3));
            const double amp = erAmp * (1 - r / (double) (er + 2)) * (rnd() < .5 ? -1 : 1);
            for (int j = 0; j < 6 && at + j < len; ++j) d[at + j] += (float) (amp * std::exp (-j * .7));
        }
    }
    // the browser's convolver normalises its impulse response (Web Audio spec): match that loudness
    double pw = 0; for (int ch = 0; ch < 2; ++ch) { auto* d = b.getReadPointer (ch); for (int i = 0; i < len; ++i) pw += (double) d[i] * d[i]; }
    pw = std::sqrt (pw / (2.0 * len)); if (pw < .000125) pw = .000125;
    b.applyGain ((float) (1.0 / pw * .00125 * 44100.0 / sr));
    return b;
}

void FxRack::timerCallback() {
    auto ip = irParams(); auto key = juce::String (pi ("fxaType")) + juce::JSON::toString (ip, true);
    if (key == irKey) return;
    irKey = key;
    auto ir = makeIR (fs, ip); irLen = ir.getNumSamples();
    conv.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    irReady = true;
}

// ---------- the effect buses (audio thread) ----------
void FxRack::sends (float* L, float* R, const float* aL, const float* aR, const float* bL, const float* bR, int n) {
    for (int off = 0; off < n; off += 2048) {
        const int m = std::min (2048, n - off);
        float* l = L + off; float* r = R + off;
        // FX A: predelay -> convolution -> tone -> return
        const bool aOn = pb ("fxaOn");
        const float aT = aOn ? p ("fxaRet") * .5f : 0.0f;
        if (aOn || aGain > 1e-4f) {
            const int pre = std::min ((int) preBuf[0].size() - 2, (int) std::lround (p ("fxaPre") / 1000.0 * fs)), N = (int) preBuf[0].size();
            float* tl = tmpA.getWritePointer (0); float* tr = tmpA.getWritePointer (1);
            for (int i = 0; i < m; ++i) {
                preBuf[0][(size_t) preW] = aL[off + i]; preBuf[1][(size_t) preW] = aR[off + i];
                int rd = preW - pre; if (rd < 0) rd += N;
                tl[i] = preBuf[0][(size_t) rd]; tr[i] = preBuf[1][(size_t) rd];
                if (++preW >= N) preW = 0;
            }
            bool loud = false; for (int i = 0; i < m && ! loud; ++i) loud = std::fabs (tl[i]) + std::fabs (tr[i]) > 1e-7f;
            aIdle = loud ? 0 : aIdle + m;
            if (irReady.load() && aIdle > irLen.load() + 4096) { std::fill (tl, tl + m, 0.0f); std::fill (tr, tr + m, 0.0f); } // tail finished: skip the convolver
            else if (irReady.load()) {
                float* chans[2] = { tl, tr };
                juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) m);
                conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
            }
            const float tone = p ("fxaTone");
            for (int c = 0; c < 2; ++c) aTone[c].set ('l', tone, .7071, 0, fs);
            for (int i = 0; i < m; ++i) { aGain += (aT - aGain) * .002f; l[i] += aTone[0].run (tl[i]) * aGain; r[i] += aTone[1].run (tr[i]) * aGain; }
        }
        // FX B as a send
        if (! pb ("fxbIns") && pb ("fxbOn")) {
            float* tl = tmpB.getWritePointer (0); float* tr = tmpB.getWritePointer (1);
            std::memcpy (tl, bL + off, sizeof (float) * (size_t) m); std::memcpy (tr, bR + off, sizeof (float) * (size_t) m);
            runB (tl, tr, m, false);
            const float ret = p ("fxbRet");
            for (int i = 0; i < m; ++i) { l[i] += tl[i] * ret; r[i] += tr[i] * ret; }
        }
    }
}

void FxRack::post (float* L, float* R, int n) {
    for (int off = 0; off < n; off += 2048) {
        const int m = std::min (2048, n - off);
        float* l = L + off; float* r = R + off;
        // FX B as an insert: the drive/flanger/phaser mix dry and wet themselves, chorus and delay add their return
        if (pb ("fxbIns") && pb ("fxbOn")) {
            const int t = pi ("fxbType");
            if (t == 1 || t == 2 || t == 4) runB (l, r, m, true);
            else {
                float* tl = tmpB.getWritePointer (0); float* tr = tmpB.getWritePointer (1);
                std::memcpy (tl, l, sizeof (float) * (size_t) m); std::memcpy (tr, r, sizeof (float) * (size_t) m);
                runB (tl, tr, m, true);
                const float ret = p ("fxbRet");
                for (int i = 0; i < m; ++i) { l[i] += tl[i] * ret; r[i] += tr[i] * ret; }
            }
        }
        // master EQ
        const float k[4] = { p ("eqLow"), p ("eqMid"), p ("eqMidHz"), p ("eqHigh") };
        if (k[0] != eqKey[0] || k[1] != eqKey[1] || k[2] != eqKey[2] || k[3] != eqKey[3]) {
            for (int c = 0; c < 2; ++c) { eqL[c].set ('L', 160, .7071, k[0], fs); eqM[c].set ('p', k[2], .9, k[1], fs); eqH[c].set ('H', 6500, .7071, k[3], fs); }
            std::copy (k, k + 4, eqKey);
        }
        if (k[0] != 0 || k[1] != 0 || k[3] != 0)
            for (int i = 0; i < m; ++i) { l[i] = eqH[0].run (eqM[0].run (eqL[0].run (l[i]))); r[i] = eqH[1].run (eqM[1].run (eqL[1].run (r[i]))); }
    }
}

namespace {
inline float hermite (const std::vector<float>& b, double rp, int mask) {
    const int i0 = (int) std::floor (rp); const float f = (float) (rp - i0);
    const float xm1 = b[(size_t) ((i0 - 1) & mask)], x0 = b[(size_t) (i0 & mask)], x1 = b[(size_t) ((i0 + 1) & mask)], x2 = b[(size_t) ((i0 + 2) & mask)];
    const float c1 = .5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - .5f * x2, c3 = .5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
inline float lfoAt (int shape, double ph) { // 0 sine, 1 tri, 2 square, 3 saw
    switch (shape) {
        case 1: return (float) (1 - 4 * std::fabs (ph - .5));
        case 2: return (float) std::tanh (5 * std::sin (2 * PI * ph));
        case 3: return (float) (2 * ph - 1);
        default: return (float) std::sin (2 * PI * ph);
    }
}
const float FL_BEATS[8] = { .5f, 1, 2, 4, 8, 16, 32, 64 };
const float PH_BEATS[7] = { .25f, .5f, 1, 2, 4, 8, 16 };
const int PH_STAGES[6] = { 4, 6, 8, 12, 16, 24 };
const int DLY_DIV[8] = { 1, 2, 3, 4, 5, 6, 8, 16 };
// drive
constexpr int OS = 4, TAPS = 48;
struct Fir { float h[TAPS]; Fir() { const double fc = .5 / OS * .9; double s = 0;
    for (int i = 0; i < TAPS; ++i) { const double m = i - (TAPS - 1) / 2.0, x = 2 * PI * fc * m; const double sinc = m == 0 ? 2 * fc : std::sin (x) / (PI * m);
        const double w = .42 - .5 * std::cos (2 * PI * i / (TAPS - 1)) + .08 * std::cos (4 * PI * i / (TAPS - 1)); h[i] = (float) (sinc * w); s += h[i]; }
    for (auto& v : h) v = (float) (v / s); } };
const Fir& fir() { static Fir f; return f; }
inline float shape (int style, float v) {
    switch (style) {
        case 1: return v >= 0 ? (1 - std::exp (-1.1f * v)) / 1.1f * 1.25f : -(1 - std::exp (1.6f * v)) / 1.6f * 1.25f;          // E console
        case 2: { const float y = v / std::sqrt (1 + v * v); return y - .08f * y * y * y; }                                     // N transformer
        case 3: return v >= 0 ? std::tanh (1.35f * v) / 1.35f : std::tanh (.8f * v) / .8f * .92f;                                // T triode
        case 4: return .6366f * std::atan (2.6f * v + .3f) - .185546f;                                                           // P pentode
        default: return std::tanh (v + .12f) - .11943f;                                                                          // A tape
    }
}
struct Eq { char t; float f, q, db; };
const std::vector<Eq> STYLE_PRE[5] = { { { 'H', 4500, .7f, 4 } }, { { 'p', 2600, .7f, 4 } }, { { 'L', 160, .7f, 3.5f } }, { { 'p', 700, .6f, 1.5f } }, { { 'p', 1100, .8f, 6 } } };
const std::vector<Eq> STYLE_POST[5] = { { { 'H', 4500, .7f, -4.5f }, { 'L', 90, .7f, 1.5f } }, { { 'p', 2600, .7f, -2 }, { 'H', 9000, .7f, 1 } }, { { 'L', 160, .7f, -2 }, { 'H', 8000, .7f, -2 } },
                                        { { 'H', 6000, .7f, -3 } }, { { 'p', 1100, .8f, -3 }, { 'H', 5000, .7f, -4 } } };
}

void FxRack::driveSetup() {
    const int st = juce::jlimit (0, 4, pi ("drStyle"));
    const float lc = p ("drLowcut"), hc = p ("drHighcut"), tone = p ("drTone");
    const bool thump = pb ("drThump"), steep = pb ("drSteep");
    for (auto& c : dr) {
        if (lc > 10.5f) { c.lc1.set ('h', lc, thump ? 1.6 : .707, 0, fs); c.lc2.set ('h', lc, .707, 0, fs); } else { c.lc1.flat(); c.lc2.flat(); }
        for (int i = 0; i < 2; ++i) { if (i < (int) STYLE_PRE[st].size()) { auto& e = STYLE_PRE[st][(size_t) i]; c.pre[i].set (e.t, e.f, e.q, e.db, fs); } else c.pre[i].flat(); }
        for (int i = 0; i < 2; ++i) { if (i < (int) STYLE_POST[st].size()) { auto& e = STYLE_POST[st][(size_t) i]; c.post[i].set (e.t, e.f, e.q, e.db, fs); } else c.post[i].flat(); }
        c.tl.set ('L', 250, .7, -tone * 6, fs); c.th.set ('H', 2500, .7, tone * 6, fs);
        if (hc < 19900) { c.hc1.set ('l', hc, .707, 0, fs); if (steep) c.hc2.set ('l', hc, .707, 0, fs); else c.hc2.flat(); } else { c.hc1.flat(); c.hc2.flat(); }
    }
}

void FxRack::delayTimes (double& tl, double& tr) const {
    const double six = 60.0 / tempo.load() / 4;
    const bool sync = pb ("dlSync");
    auto side = [&] (const char* div, const char* off, const char* ms) { return sync ? DLY_DIV[juce::jlimit (0, 7, pi (div))] * six * (1 + p (off) / 100.0) : p (ms) / 1000.0; };
    tl = side ("dlDivL", "dlOffL", "dlMsL"); tr = pb ("dlLink") ? tl : side ("dlDivR", "dlOffR", "dlMsR");
    tl = std::min (4.9, std::max (.001, tl)); tr = std::min (4.9, std::max (.001, tr));
}

void FxRack::runB (float* L, float* R, int n, bool insert) {
    const int t = juce::jlimit (0, 4, pi ("fxbType"));
    if (t != bType) { // a different effect: start it from silence
        bType = t;
        for (auto& b : chBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : flBuf) std::fill (b.begin(), b.end(), 0.0f);
        std::memset (phZ, 0, sizeof phZ); phFb[0] = phFb[1] = 0;
        for (auto& s : dl) { std::fill (s.buf.begin(), s.buf.end(), 0.0f); s.lastLp = s.lastMix = 0; s.hp.reset(); s.lp.reset(); }
        drKey = {};
    }
    const float bpm = tempo.load();
    if (t == 0) { // chorus: two slowly swaying delays, one to each side
        const double rate = p ("chRate"), dep = .0055 * p ("chDepth") / 100.0; const int M = 8191;
        for (int i = 0; i < n; ++i) {
            const float x = L[i] + R[i];
            chBuf[0][(size_t) chW] = chBuf[1][(size_t) chW] = x;
            float out[2];
            for (int s = 0; s < 2; ++s) {
                chPh[s] += rate * (s ? 1.17 : 1) / fs; if (chPh[s] >= 1) chPh[s] -= 1;
                const double d = (.016 + s * .005 + dep * std::sin (2 * PI * chPh[s])) * fs;
                out[s] = hermite (chBuf[s], chW - d, M);
            }
            L[i] = out[0]; R[i] = out[1]; chW = (chW + 1) & M;
        }
    } else if (t == 1) { // flanger: jet sweeps in beats, high-passed soft-limited feedback
        const bool sync = pb ("flSync");
        const double inc = (sync ? bpm / 60.0 / FL_BEATS[juce::jlimit (0, 7, pi ("flBeats"))] : p ("flRate")) / fs;
        const float fb = juce::jlimit (-.95f, .95f, p ("flFb") / 100), mix = insert ? p ("flMix") : 1, depth = p ("flDepth") / 100, spread = p ("flSpread");
        const int M = 8191;
        for (int i = 0; i < n; ++i) {
            flPh += inc; if (flPh >= 1) flPh -= 1;
            for (int c = 0; c < 2; ++c) {
                const float x = c ? R[i] : L[i];
                double u = flPh + (c ? spread / 400.0 : 0); if (u >= 1) u -= 1;
                const double tri = 1 - std::fabs (2 * u - 1), d = .0002 * std::pow (2.0, tri * depth * 5.3) * fs;
                const float dlv = hermite (flBuf[c], flW - d, M);
                const float hpv = dlv - flHpx[c] + .985f * flHp[c]; flHpx[c] = dlv; flHp[c] = hpv;
                flBuf[c][(size_t) flW] = x + std::tanh (fb * hpv);
                const float y = mix < 1 ? x * (1 - mix) + dlv * mix : dlv;
                if (c) R[i] = y; else L[i] = y;
            }
            flW = (flW + 1) & M;
        }
    } else if (t == 2) { // phaser: allpass stages, resonance, LFO or envelope, analog drift
        const int shp = juce::jlimit (0, 4, pi ("phShape")); const bool env = shp == 4, an = pb ("phAnalog"), sync = pb ("phSync");
        const double inc = (sync ? bpm / 60.0 / PH_BEATS[juce::jlimit (0, 6, pi ("phBeats"))] : p ("phRate")) / fs;
        const int st = PH_STAGES[juce::jlimit (0, 5, pi ("phStages"))];
        const float res = juce::jlimit (-.95f, .95f, p ("phRes") / 100), mix = insert ? p ("phMix") : 1, span = p ("phDepth") / 100 * 3.5f, freq = p ("phFreq"), width = p ("phWidth");
        for (int i = 0; i < n; ++i) {
            phPh += inc; if (phPh >= 1) phPh -= 1;
            const float xl = L[i], xr = R[i];
            if (env) { const float lv = std::fabs (xl) + std::fabs (xr); phEnv += (lv > phEnv ? .01f : .0004f) * (lv - phEnv); }
            if (an && (i & 63) == 0) { phDv += (rnd.nextFloat() - .5f) * .002f; phDv *= .995f; phDrift = juce::jlimit (-.04f, .04f, phDrift + phDv); }
            for (int c = 0; c < 2; ++c) {
                if ((i & 7) == 0) {
                    float l;
                    if (env) l = std::min (1.0f, phEnv * 3) * 2 - 1;
                    else { double u = phPh + (c ? width / 200.0 : 0); if (u >= 1) u -= 1; l = lfoAt (shp, u); }
                    const double f = std::max (25.0, std::min (fs * .42, freq * std::pow (2.0, span * l) * (1 + phDrift)));
                    const double tt = std::tan (PI * f / fs); phA[c] = (float) ((tt - 1) / (tt + 1));
                }
                const float a = phA[c], x = c ? xr : xl; float* z = phZ[c];
                float fbv = phFb[c]; fbv = an ? std::tanh (fbv * 1.3f) / 1.3f : juce::jlimit (-1.5f, 1.5f, fbv);
                float v = x + res * fbv;
                for (int s = 0; s < st; ++s) { const float y = a * v + z[s]; z[s] = v - a * y; v = y; }
                phFb[c] = v;
                if (an) v = std::tanh (v * .9f) / .9f;
                const float y = mix < 1 ? x * (1 - mix) + v * mix : v;
                if (c) R[i] = y; else L[i] = y;
            }
        }
    } else if (t == 3) { // stereo delay
        double want[2]; delayTimes (want[0], want[1]);
        const bool pp = pb ("dlPP"), freeze = pb ("dlFreeze"), filt = pb ("dlFilt"); const int mode = pi ("dlMode");
        const float fbv = p ("dlFb") / 100, mrate = p ("dlMRate"), mtime = p ("dlMTime") / 100 * .006f, mfilt = filt ? p ("dlMFilt") / 100 * 2400 : 0;
        const float ffreq = p ("dlFreq"), fw = p ("dlWidth");
        const double lo = filt ? ffreq / std::pow (2.0, fw / 2) : 10, hi = filt ? ffreq * std::pow (2.0, fw / 2) : 22000;
        const double xfade = mode == 2 ? .004 : .06;
        for (int s = 0; s < 2; ++s) { // a new time: glide (repitch) or crossfade to the other read head
            auto& S = dl[s];
            if (mode == 0) { S.target = want[s]; continue; }
            if (std::fabs (want[s] - S.time[S.active]) < std::max (1e-4, S.time[S.active] * .002) || dlClock < S.busy) continue;
            const int b = 1 - S.active; S.time[b] = want[s]; S.active = b; S.gStep = (float) (1.0 / (xfade * fs)); S.busy = dlClock + xfade + .01;
        }
        const int N = (int) dl[0].buf.size();
        for (int i = 0; i < n; ++i) {
            dlPh += mrate / fs; if (dlPh >= 1) dlPh -= 1;
            const float lfo = (float) std::sin (2 * PI * dlPh);
            if ((dlCoefCount++ & 31) == 0) {
                const double m = std::pow (2.0, lfo * mfilt / 1200.0);
                for (auto& S : dl) { S.hp.set ('h', std::max (10.0, std::min (20000.0, lo * m)), .7071, 0, fs); S.lp.set ('l', std::max (40.0, std::min (22000.0, hi * m)), .7071, 0, fs); }
            }
            const float in[2] = { pp ? (L[i] + R[i]) * .5f : L[i], pp ? 0.0f : R[i] };
            float outv[2];
            for (int s = 0; s < 2; ++s) {
                auto& S = dl[s]; auto& F = dl[pp ? 1 - s : s]; // feedback comes from the other side for ping pong
                const float fbIn = freeze ? F.lastMix : F.lastLp * fbv;
                S.buf[(size_t) S.w] = (freeze ? 0.0f : in[s]) + fbIn;
                if (mode == 0) { S.time[S.active] += (S.target - S.time[S.active]) * (1 - std::exp (-1.0 / (.07 * fs))); }
                float mix = 0;
                for (int h = 0; h < 2; ++h) {
                    const float gt = h == S.active ? 1.0f : 0.0f;
                    if (S.g[h] != gt) S.g[h] = gt > S.g[h] ? std::min (gt, S.g[h] + (S.gStep > 0 ? S.gStep : 1)) : std::max (gt, S.g[h] - (S.gStep > 0 ? S.gStep : 1));
                    if (S.g[h] <= 0) continue;
                    double rp = S.w - (S.time[h] + lfo * mtime) * fs; while (rp < 0) rp += N;
                    const int i0 = (int) rp; const float fr = (float) (rp - i0);
                    const float a = S.buf[(size_t) (i0 % N)], b = S.buf[(size_t) ((i0 + 1) % N)];
                    mix += (a + (b - a) * fr) * S.g[h];
                }
                const float y = S.lp.run (S.hp.run (mix));
                S.lastMix = mix; S.lastLp = y; outv[s] = y;
                if (++S.w >= N) S.w = 0;
            }
            L[i] = outv[0]; R[i] = outv[1];
            dlClock += 1 / fs;
        }
    } else { // drive
        auto key = juce::String (pi ("drStyle")) + "|" + juce::String (p ("drLowcut")) + "|" + juce::String (p ("drHighcut")) + "|" + juce::String (p ("drTone")) + "|" + juce::String ((int) pb ("drThump")) + juce::String ((int) pb ("drSteep"));
        if (key != drKey) { drKey = key; driveSetup(); }
        const int st = juce::jlimit (0, 4, pi ("drStyle")); const bool autoG = pb ("drAuto");
        const float gT = std::pow (10.0f, (p ("drDrive") * 3 + (pb ("drPunish") ? 20 : 0)) / 20) * .5f, mT = insert ? p ("drMix") : 1, oT = std::pow (10.0f, p ("drOut") / 20);
        const auto& H = fir().h;
        float pin = drPin, pout = drPout;
        for (int i = 0; i < n; ++i) {
            drG += (gT - drG) * .002f; drM += (mT - drM) * .002f; drO += (oT - drO) * .002f;
            for (int c = 0; c < 2; ++c) {
                auto& C = dr[c]; const float dry = c ? R[i] : L[i];
                float v = C.lc2.run (C.lc1.run (dry));
                v = C.pre[1].run (C.pre[0].run (v)) * drG;
                std::memmove (C.up + 1, C.up, sizeof (float) * (TAPS / OS - 1)); C.up[0] = v;
                for (int ph = 0; ph < OS; ++ph) {
                    float u = 0; for (int k = 0; k < TAPS / OS; ++k) u += H[ph + OS * k] * C.up[k];
                    std::memmove (C.dn + 1, C.dn, sizeof (float) * (TAPS - 1)); C.dn[0] = shape (st, u * OS);
                }
                float w = 0; for (int k = 0; k < TAPS; ++k) w += H[k] * C.dn[k];
                const float hp = w - C.dcx + .9995f * C.dc; C.dcx = w; C.dc = hp; w = hp;
                w = C.post[1].run (C.post[0].run (w));
                w = C.th.run (C.tl.run (w));
                w = C.hc2.run (C.hc1.run (w));
                if (c == 0) { pin += (dry * dry - pin) * .0002f; pout += (w * w - pout) * .0002f; }
                const float wet = w * (autoG ? drComp : 1 / std::max (1.0f, drG * .5f));
                const float y = (dry * (1 - drM) + wet * drM) * drO;
                if (c) R[i] = y; else L[i] = y;
            }
        }
        drPin = pin; drPout = pout;
        if (autoG && drPout > 1e-9f) { const float target = std::min (4.0f, std::max (.05f, std::sqrt (drPin / drPout))); drComp += (target - drComp) * .05f; }
    }
}

} // namespace xfx
