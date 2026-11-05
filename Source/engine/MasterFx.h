// Output stage, matching the browser version: Analog colour (warmth, weight, sub, smooth, mono bass),
// then the bus compressor (Web Audio's DynamicsCompressor settings, with its automatic make-up gain).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace exw {

struct RBJ { // RBJ biquad (same as the browser's worklet code)
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (char type, double f, double q, double db, double fs) {
        f = std::max (10.0, std::min (fs * .45, f));
        double w = 2 * 3.14159265358979 * f / fs, cw = std::cos (w), sw = std::sin (w), A = std::pow (10.0, db / 40), al = sw / (2 * q);
        double B0, B1, B2, A0, A1, A2;
        if (type == 'l') { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; }
        else if (type == 'p') { B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; }
        else { // shelves: 'L' low, 'H' high
            double s = std::sqrt (A) * 2 * al, sg = type == 'L' ? -1 : 1;
            B0 = A * ((A + 1) + sg * (A - 1) * cw + s); B1 = -2 * sg * A * ((A - 1) + sg * (A + 1) * cw); B2 = A * ((A + 1) + sg * (A - 1) * cw - s);
            A0 = (A + 1) - sg * (A - 1) * cw + s; A1 = 2 * sg * ((A - 1) - sg * (A + 1) * cw); A2 = (A + 1) - sg * (A - 1) * cw - s;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void flat() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    float run (float x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};

class MasterFx {
public:
    struct Params { bool on = true, mono = true; float warmth = 35, weight = 40, sub = 25, smooth = 20; };

    void prepare (double rate) {
        fs = rate; dirty = true;
        for (auto& c : ch) { std::memset (c.up, 0, sizeof c.up); std::memset (c.dn, 0, sizeof c.dn); c.dc = c.dcx = 0; }
        // FIR for 4x oversampling (Blackman-windowed sinc)
        double s = 0;
        for (int i = 0; i < CT; ++i) { double m = i - (CT - 1) / 2.0, x = 2 * 3.14159265358979 * fc * m; double sinc = m == 0 ? 2 * fc : std::sin (x) / (3.14159265358979 * m);
            double w = .42 - .5 * std::cos (2 * 3.14159265358979 * i / (CT - 1)) + .08 * std::cos (4 * 3.14159265358979 * i / (CT - 1)); fir[i] = (float) (sinc * w); s += fir[i]; }
        for (int i = 0; i < CT; ++i) fir[i] = (float) (fir[i] / s);
        compEnv = 0; attackCoef = std::exp (-1.0 / (.004 * fs)); releaseCoef = std::exp (-1.0 / (.25 * fs));
    }
    void setParams (const Params& np) { if (np.warmth != p.warmth || np.weight != p.weight || np.smooth != p.smooth || np.sub != p.sub || np.mono != p.mono || np.on != p.on) { p = np; dirty = true; } }

    void process (float* L, float* R, int n) {
        if (dirty) setup();
        if (p.on) colour (L, R, n);
        compress (L, R, n);
    }

private:
    static constexpr int CT = 32; static constexpr double fc = .5 / 4 * .9;
    struct Ch { RBJ pre, post, sh, bump, mud, sm1, sm2, xo, xo2; float up[CT / 4] {}, dn[CT] {}; double dc = 0, dcx = 0; };
    Params p; double fs = 44100; bool dirty = true;
    Ch ch[2]; RBJ sl1, sl2, so1, so2; float fir[CT] {};
    double g = 1, comp = 1, pin = 1e-7, pout = 1e-7; float flip = 1, env = 0; bool armed = true;
    double compEnv = 0, attackCoef = 0, releaseCoef = 0;

    void setup() {
        double w = p.weight / 100, sm = p.smooth / 100;
        for (auto& c : ch) {
            c.pre.set ('H', 3000, .7, 3, fs); c.post.set ('H', 3000, .7, -3, fs);
            c.sh.set ('L', 95, .7, w * 6, fs); c.bump.set ('p', 58, 1.1, w * 2.5, fs); c.mud.set ('p', 260, .9, -w * 2, fs);
            double f = 22000 * std::pow (6500.0 / 22000.0, sm);
            if (sm > .01) { c.sm1.set ('l', f, .6, 0, fs); c.sm2.set ('l', f * 1.6, .707, 0, fs); } else { c.sm1.flat(); c.sm2.flat(); }
            c.xo.set ('l', 120, .707, 0, fs); c.xo2.set ('l', 120, .707, 0, fs);
        }
        sl1.set ('l', 140, .707, 0, fs); sl2.set ('l', 140, .707, 0, fs); so1.set ('l', 85, .707, 0, fs); so2.set ('l', 85, .707, 0, fs);
        dirty = false;
    }

    void colour (float* L, float* R, int n) {
        const double gT = std::pow (10.0, p.warmth / 100 * 24 / 20); const float subA = p.sub / 100 * .9f; const bool warm = p.warmth > .5f;
        for (int i = 0; i < n; ++i) {
            g += (gT - g) * .001;
            float l = L[i], r = R[i];
            if (p.mono) { float ll = ch[0].xo2.run (ch[0].xo.run (l)), rl = ch[1].xo2.run (ch[1].xo.run (r)), m = (ll + rl) * .5f; l = l - ll + m; r = r - rl + m; }
            float sub = 0;
            if (subA > 0) {
                float low = sl2.run (sl1.run ((l + r) * .5f)), a = std::fabs (low);
                env += (a > env ? .004f : .0006f) * (a - env);
                float th = env * .25f;
                if (armed && low > th) { flip = -flip; armed = false; } else if (low < -th) armed = true;
                sub = so2.run (so1.run (flip * env)) * subA;
            }
            float xs[2] = { l + sub, r + sub };
            for (int c = 0; c < 2; ++c) {
                auto& C = ch[c]; float v = C.mud.run (C.bump.run (C.sh.run (xs[c])));
                if (warm) {
                    float dry = v, u0 = C.pre.run (v) * (float) g;
                    std::memmove (C.up + 1, C.up, sizeof (float) * (CT / 4 - 1)); C.up[0] = u0;
                    for (int ph = 0; ph < 4; ++ph) {
                        float u = 0; for (int k = 0; k < CT / 4; ++k) u += fir[ph + 4 * k] * C.up[k]; u *= 4;
                        std::memmove (C.dn + 1, C.dn, sizeof (float) * (CT - 1)); C.dn[0] = std::tanh (u + .06f) - .05993f;
                    }
                    float w = 0; for (int k = 0; k < CT; ++k) w += fir[k] * C.dn[k];
                    double hp = w - C.dcx + .9997 * C.dc; C.dcx = w; C.dc = hp;
                    v = C.post.run ((float) hp);
                    if (c == 0) { pin += (dry * dry - pin) * .00005; pout += (v * v - pout) * .00005; }
                    v *= (float) comp;
                }
                v = C.sm2.run (C.sm1.run (v));
                (c ? R : L)[i] = v;
            }
            if (warm && (i & 127) == 127 && pout > 1e-12) { double t = std::min (2.0, std::max (.05, std::sqrt (pin / pout))); comp += (t - comp) * .03; }
        }
    }

    // Web Audio DynamicsCompressor at threshold -10 dB, knee 8, ratio 20, attack 4 ms, release 250 ms, with its make-up gain
    void compress (float* L, float* R, int n) {
        const double thr = -10, knee = 8, ratio = 20;
        auto curve = [&] (double x) { if (x <= thr) return x; if (x < thr + knee) { double d = x - thr; return thr + d - (1 - 1 / ratio) * d * d / (2 * knee); } double yk = thr + knee - (1 - 1 / ratio) * knee / 2; return yk + (x - thr - knee) / ratio; };
        static const double makeup = std::pow (1.0 / std::pow (10.0, curve (0) / 20), .6);
        for (int i = 0; i < n; ++i) {
            double a = std::max (std::fabs (L[i]), std::fabs (R[i])), x = 20 * std::log10 (a + 1e-12), red = x - curve (x); // dB of reduction wanted
            compEnv = red > compEnv ? attackCoef * compEnv + (1 - attackCoef) * red : releaseCoef * compEnv + (1 - releaseCoef) * red;
            double gl = std::pow (10.0, -compEnv / 20) * makeup;
            L[i] = (float) (L[i] * gl); R[i] = (float) (R[i] * gl);
        }
    }
};

} // namespace exw
