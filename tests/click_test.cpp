// Click hunt: replaces every sample with a clean sine (so any spike is the engine's doing), then plays repeated
// notes and chords in host-sized blocks with MIDI at odd offsets, and reports discontinuities.
#include "../Source/engine/Engine.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
using namespace exw;
int main (int argc, char** argv) {
    auto bank = loadBank (argc > 1 ? argv[1] : "/tmp/claude-0/xl/xl.exb");
    if (! bank->error.empty()) { printf ("%s\n", bank->error.c_str()); return 1; }
    auto* mb = const_cast<Bank*> (bank.get());
    for (auto& sp : mb->samples) { auto& s = *sp; double f = 220; int ls = 0;
        if (s.loop && s.loopEnd > s.loopStart) { int len = s.loopEnd + 1 - s.loopStart; int cyc = std::max (1, (int) std::lround (len * 220.0 / s.rate)); f = cyc * (double) s.rate / len; ls = s.loopStart; }
        for (int i = 0; i < s.frames; ++i) { auto v = (int16_t) std::lround (16000 * std::sin (2 * M_PI * f * (i - ls) / s.rate) * std::min (1.0, i / 64.0)); s.L[(size_t) i] = v; if (! s.R.empty()) s.R[(size_t) i] = v; } }
    Settings set; set.colOn = argc > 3 ? atoi (argv[3]) : 1; Engine eng (set); const int sr = 48000; eng.prepare (sr); eng.setBank (bank);
    const char* names[] = { "raw:Basic Saw", "syn:ORiON", "bas:FATSO mono", "pad:Strings Box", "led:Wet", "bts:Trancer" };
    int blk = argc > 2 ? atoi (argv[2]) : 512;
    std::mt19937 rng (1);
    for (auto nm : names) {
        int idx = -1; for (size_t i = 0; i < bank->presets.size(); ++i) if (bank->presets[i].name == nm) idx = (int) i;
        if (idx < 0) continue; static Program pg; pg = Program(); pg.version++; pg.nSlots = 1; pg.slots[0].preset = &bank->presets[(size_t) idx]; pg.slots[0].isPreset = true; eng.setProgram (&pg, true, pg.slots[0].preset && pg.slots[0].preset->hasIc ? pg.slots[0].preset->ic.data() : nullptr);
        const int n = sr * 4; std::vector<float> L (n), R (n);
        // events: chord every 250 ms held 150 ms, plus a fast repeated note
        std::vector<std::tuple<int, int, int>> ev; // sample, note, vel (0 = off)
        for (int t = 0; t < n - sr / 2; t += sr / 4) { for (int k : { 48, 52, 55 }) { ev.push_back ({ t + (int) (rng() % 300), k, 100 }); ev.push_back ({ t + sr * 15 / 100, k, 0 }); } }
        for (int t = sr / 8; t < n - sr / 2; t += sr / 10) { ev.push_back ({ t, 64, 110 }); ev.push_back ({ t + sr / 20, 64, 0 }); }
        std::sort (ev.begin(), ev.end());
        size_t e = 0;
        for (int i = 0; i < n; i += blk) {
            int end = std::min (n, i + blk), pos = i;
            while (e < ev.size() && std::get<0> (ev[e]) < end) { int at = std::max (pos, std::get<0> (ev[e])); if (at > pos) { eng.render (L.data() + pos, R.data() + pos, at - pos); pos = at; } if (std::get<2> (ev[e])) eng.noteOn (std::get<1> (ev[e]), std::get<2> (ev[e])); else eng.noteOff (std::get<1> (ev[e])); ++e; }
            if (end > pos) eng.render (L.data() + pos, R.data() + pos, end - pos);
        }
        // discontinuity score: second difference vs. what a 220-1000 Hz sine could do
        std::vector<double> d (n); double mx = 0; int spikes = 0, at = 0; double pk = 0;
        for (int i = 1; i < n - 1; ++i) { d[i] = std::fabs (L[i + 1] - 2 * L[i] + L[i - 1]); pk = std::max (pk, (double) std::fabs (L[i])); }
        for (int i = 1; i < n - 1; ++i) { double lim = pk * .012 + 1e-5; if (d[i] > lim) { ++spikes; if (d[i] > mx) { mx = d[i]; at = i; } i += 32; } }
        { std::string fn = std::string ("/tmp/claude-0/pool/ck_") + nm + ".f32"; for (auto& c : fn) if (c == ':' || c == ' ') c = '_'; FILE* f = fopen (fn.c_str(), "wb"); fwrite (L.data(), 4, L.size(), f); fclose (f); }
        printf ("%-16s block %4d  peak %.3f  clicks %3d  worst %.4f at %.3fs (sample %%16=%d)\n", nm, blk, pk, spikes, mx, at / (double) sr, at % 16);
        eng.allNotesOff(); std::vector<float> a (8192), b (8192); for (int k = 0; k < 20; ++k) eng.render (a.data(), b.data(), 8192);
    }
}
