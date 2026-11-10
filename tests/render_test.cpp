// Offline check of the engine: loads the bank, plays notes, writes WAVs and prints levels.
#include "../Source/engine/Engine.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
using namespace exw;
static void wav (const std::string& p, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    std::ofstream f (p, std::ios::binary); int n = (int) L.size(); int data = n * 4;
    auto u32 = [&] (uint32_t v) { f.write ((char*) &v, 4); }; auto u16 = [&] (uint16_t v) { f.write ((char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (data);
    for (int i = 0; i < n; ++i) { int16_t a = (int16_t) std::lround (std::max (-1.f, std::min (1.f, L[i])) * 32767), b = (int16_t) std::lround (std::max (-1.f, std::min (1.f, R[i])) * 32767); f.write ((char*) &a, 2); f.write ((char*) &b, 2); }
}
int main (int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "/tmp/claude-0/pool/x/Xtreme_Lead-1.exb";
    auto bank = loadBank (path);
    if (! bank->error.empty()) { printf ("error: %s\n", bank->error.c_str()); return 1; }
    printf ("presets %zu samples %zu missing zones %d\n", bank->presets.size(), bank->samples.size(), bank->missingZones);
    Settings set; Engine eng (set); const int sr = 48000; eng.prepare (sr); eng.setBank (bank);
    auto find = [&] (const char* nm) { for (size_t i = 0; i < bank->presets.size(); ++i) if (bank->presets[i].name == nm) return (int) i; return -1; };
    struct T { const char* preset; std::vector<int> notes; int vel; double secs; const char* setup; };
    std::vector<T> tests = { { "raw:Basic Saw", { 48 }, 110, 2.0, "" }, { "syn:ORiON", { 48, 55, 60, 64 }, 100, 3.0, "" }, { "bas:FATSO mono", { 36, 43 }, 110, 2.0, "" },
                             { "led:Wet", { 60 }, 100, 2.0, "" }, { "syn:ORiON", { 48, 55, 60, 64 }, 100, 3.0, "dark" }, { "raw:Basic Saw", { 48 }, 110, 2.0, "dark" } };
    static Program pg; int ver = 0;
    auto single = [&] (int idx) { pg = Program(); pg.version = ++ver; pg.nSlots = 1; pg.slots[0].preset = &bank->presets[(size_t) idx]; pg.slots[0].isPreset = true; };
    auto rms = [] (std::vector<float>& a) { double e = 0; for (float x : a) e += x * x; return std::sqrt (e / a.size()); };
    { // a two-layer patch: Basic Saw + Strings Box an octave up, the strings split to the upper keys
        pg = Program(); pg.version = ++ver; pg.patch = true; pg.nSlots = 4;
        for (auto& s : pg.slots) s.layer = true;
        pg.slots[0].preset = &bank->presets[(size_t) find ("raw:Basic Saw")]; pg.slots[0].isPreset = true;
        pg.slots[1].preset = &bank->presets[(size_t) find ("pad:Strings Box")]; pg.slots[1].isPreset = true; pg.slots[1].coarse = 12; pg.slots[1].key.lo = 60;
        eng.setProgram (&pg, true, pg.slots[0].preset && pg.slots[0].preset->hasIc ? pg.slots[0].preset->ic.data() : nullptr);
        std::vector<float> a (4800), b (4800);
        eng.noteOn (48, 100); eng.render (a.data(), b.data(), 4800); int low = eng.activePlayers();
        eng.noteOn (64, 100); eng.render (a.data(), b.data(), 4800); int both = eng.activePlayers();
        static Program p2; p2 = pg; p2.version = ++ver; p2.slots[0].volDb = -48; eng.setProgram (&p2, false, nullptr); for (int k = 0; k < 4; ++k) eng.render (a.data(), b.data(), 4800);
        printf ("patch: players after low note %d, after high note %d (layer 2 only above C3), rms with layer 1 muted %.4f\n", low, both, rms (a));
        eng.allNotesOff(); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 4800);
    }
    { // one layer with its own filter (lowpass, closed), then its own slow envelope, then an LFO on volume
        auto run = [&] (const char* label, auto setup) {
            pg = Program(); pg.version = ++ver; pg.patch = true; pg.nSlots = 4; for (auto& s : pg.slots) s.layer = true;
            pg.slots[0].preset = &bank->presets[(size_t) find ("raw:Basic Saw")]; pg.slots[0].isPreset = true; setup (pg.slots[0]);
            eng.setProgram (&pg, true, pg.slots[0].preset && pg.slots[0].preset->hasIc ? pg.slots[0].preset->ic.data() : nullptr);
            std::vector<float> a (2400), b (2400); std::vector<double> r; double d = 0, e = 0; eng.noteOn (48, 100);
            for (int k = 0; k < 40; ++k) { eng.render (a.data(), b.data(), 2400); r.push_back (rms (a)); for (int i = 1; i < 2400; ++i) { d += (a[i] - a[i - 1]) * (a[i] - a[i - 1]); e += a[i] * a[i]; } }
            printf ("%-30s rms@50ms %.4f @500ms %.4f @1s %.4f  min %.4f max %.4f  brightness %.3f\n", label, r[1], r[10], r[19], *std::min_element (r.begin() + 2, r.end()), *std::max_element (r.begin() + 2, r.end()), std::sqrt (d / (e + 1e-12)));
            eng.allNotesOff(); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 2400);
        };
        run ("layer as preset", [] (Slot&) {});
        run ("layer filter lp4 closed", [] (Slot& s) { s.filtFromPreset = false; s.filt.type = F_LP4; s.filt.freq = 20; s.filt.res = 10; });
        run ("layer filter lp4 + env sweep", [] (Slot& s) { s.filtFromPreset = false; s.filt.type = F_LP4; s.filt.freq = 20; s.filt.res = 40; s.filt.env = 60; s.filt.decay = 1.5f; });
        run ("layer morph Ah>Ee", [] (Slot& s) { s.filtFromPreset = false; s.filt.type = F_MORPH; s.filt.morph = 0; s.filt.freq = 50; });
        run ("layer env: 0.8 s attack", [] (Slot& s) { s.useSrcEnv = false; s.env.s[0] = .8f; });
        run ("layer LFO1 square 4 Hz vol", [] (Slot& s) { s.lfo[0].shape = 2; s.lfo[0].rate = 4; s.lfo[0].vol = 100; });
        run ("layer FG pitch steps", [] (Slot& s) { s.fg[0].steps[1] = .5f; s.fg[0].steps[3] = 1; s.fg[0].end = 4; s.fg[0].pitch = 12; s.fg[0].vol = 50; });
        pg.mf = FiltSpec();
    }
    { // patch master filter, lowpass closed
        pg = Program(); pg.version = ++ver; pg.patch = true; pg.nSlots = 4; for (auto& s : pg.slots) s.layer = true;
        pg.slots[0].preset = &bank->presets[(size_t) find ("raw:Basic Saw")]; pg.slots[0].isPreset = true; pg.mf.type = F_LP4; pg.mf.freq = 15;
        eng.setProgram (&pg, true, pg.slots[0].preset && pg.slots[0].preset->hasIc ? pg.slots[0].preset->ic.data() : nullptr); std::vector<float> a (24000), b (24000); eng.noteOn (48, 100); eng.render (a.data(), b.data(), 24000);
        double d = 0, e = 0; for (int i = 1; i < 24000; ++i) { d += (a[i] - a[i - 1]) * (a[i] - a[i - 1]); e += a[i] * a[i]; }
        printf ("%-30s rms %.4f brightness %.3f\n", "patch master filter closed", rms (a), std::sqrt (d / (e + 1e-12)));
        eng.allNotesOff(); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 2400);
    }
    for (auto& t : tests) {
        int idx = find (t.preset); if (idx < 0) { printf ("missing %s\n", t.preset); continue; }
        single (idx); if (std::string (t.setup) == "dark") { pg.ofsFreq = -4.0f / (std::log2 (500.0f) * 12) * 100 * 12; }
        eng.setProgram (&pg, true, pg.slots[0].preset && pg.slots[0].preset->hasIc ? pg.slots[0].preset->ic.data() : nullptr);
        int n = (int) (t.secs * sr); std::vector<float> L (n), R (n);
        const int blk = 256; int off = (int) (t.secs * .7 * sr);
        for (int i = 0; i < n; i += blk) {
            if (i == 0) for (int k : t.notes) eng.noteOn (k, t.vel);
            if (i <= off && i + blk > off) for (int k : t.notes) eng.noteOff (k);
            eng.render (L.data() + i, R.data() + i, std::min (blk, n - i));
        }
        double e = 0, pk = 0, d = 0; int nan = 0; for (int i = 0; i < n; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) ++nan; e += L[i] * L[i] + R[i] * R[i]; if (i) d += (L[i] - L[i - 1]) * (L[i] - L[i - 1]); pk = std::max (pk, (double) std::max (std::fabs (L[i]), std::fabs (R[i]))); }
        std::string out = std::string ("/tmp/claude-0/pool/cpp_") + t.preset + t.setup + ".wav"; for (auto& c : out) if (c == ':' || c == ' ') c = '_';
        wav (out, L, R, sr);
        printf ("%-16s %-4s rms %.4f peak %.3f brightness %.3f nan %d notes-left %d\n", t.preset, t.setup, std::sqrt (e / (2.0 * n)), pk, std::sqrt (d / (e / 2 + 1e-12)), nan, eng.activeNotes());
        eng.allNotesOff(); std::vector<float> a (8192), b (8192); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 8192);
    }
}
