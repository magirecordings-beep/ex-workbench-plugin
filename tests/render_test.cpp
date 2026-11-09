// Offline check of the engine: loads the bank, plays notes, writes WAVs and prints levels.
#include "../Source/engine/Engine.h"
#include <cstdio>
#include <cmath>
#include <cstring>
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
    { // a two-layer patch: Basic Saw + Strings Box an octave up, the strings split to the upper keys
        Patch pt; pt.active = true; pt.layers[0].preset = find ("raw:Basic Saw"); pt.layers[1].preset = find ("pad:Strings Box"); pt.layers[1].coarse = 12; pt.layers[1].keyLo = 60;
        eng.setPatch (pt, true);
        std::vector<float> a (4800), b (4800);
        eng.noteOn (48, 100); eng.render (a.data(), b.data(), 4800); int low = eng.activePlayers();
        eng.noteOn (64, 100); eng.render (a.data(), b.data(), 4800); int both = eng.activePlayers();
        pt.layers[0].volDb = -48; eng.setPatch (pt, false); for (int k = 0; k < 4; ++k) eng.render (a.data(), b.data(), 4800);
        double e = 0; for (float x : a) e += x * x;
        printf ("patch: players after low note %d, after high note %d (layer 2 only above C3), rms with layer 1 muted %.4f\n", low, both, std::sqrt (e / a.size()));
        eng.allNotesOff(); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 4800);
        eng.setPreset (0);
    }
    for (auto& t : tests) {
        int idx = find (t.preset); if (idx < 0) { printf ("missing %s\n", t.preset); continue; }
        set.cutShiftOct = std::string (t.setup) == "dark" ? -4.0f : 0.0f;
        eng.setPreset (idx);
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
