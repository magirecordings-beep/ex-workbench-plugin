// Arpeggiator, as in the browser version: plays the held keys one at a time (or as repeated chords) in time
// with the host tempo. Works on the MIDI before it reaches the sound engine, with sample-accurate steps.
#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <random>
#include <vector>

class Arp {
public:
    struct Params { int mode = 0; int pattern = 0; int rate = 3; int oct = 1; float gate = 60, swing = 0; bool hostSync = true; float tempo = 120; };
    // pattern: 0 up, 1 down, 2 up+down, 3 down+up, 4 as played, 5 random, 6 chord   rate: 1/4 1/8 1/8T 1/16 1/16T 1/32
    static double rateBeats (int r) { static const double v[6] = { 1, .5, 1.0 / 3, .25, 1.0 / 6, .125 }; return v[juce::jlimit (0, 5, r)]; }

    void prepare (double rate) { sr = rate; reset(); }
    void reset() { keys.clear(); offs.clear(); ons.clear(); playingNotes.clear(); running = false; step = 0; toNext = 0; }
    int heldCount() const { return (int) keys.size(); }

    // transforms `midi` in place: note on/off feed the arpeggiator, everything else passes through
    void process (juce::MidiBuffer& midi, int n, const Params& p, double bpm, std::optional<double> ppq, bool hostPlaying) {
        juce::MidiBuffer out;
        if (p.mode == 0) {
            if (wasOn) { for (int note : playingNotes) out.addEvent (juce::MidiMessage::noteOff (1, note), 0); playingNotes.clear(); offs.clear(); ons.clear(); keys.clear(); running = false; wasOn = false;
                         for (auto m : midi) out.addEvent (m.getMessage(), m.samplePosition); midi.swapWith (out); }
            return;
        }
        wasOn = true;
        if (! p.hostSync) bpm = p.tempo;
        bpm = juce::jlimit (20.0, 400.0, bpm > 0 ? bpm : 120.0);
        const double stepSamples = 60.0 / bpm * rateBeats (p.rate) * sr;
        // host playing: keep the steps on the host's grid
        if (p.hostSync && hostPlaying && ppq && running) {
            const double beats = rateBeats (p.rate), pos = *ppq / beats, frac = pos - std::floor (pos);
            double want = (frac < 1e-6 ? 0 : 1 - frac) * stepSamples;
            if (std::fabs (want - toNext) > 64 && std::fabs (want - toNext) < stepSamples - 64) toNext = want; // drifted (loop, jump): snap
        }
        auto events = std::vector<std::pair<int, juce::MidiMessage>>();
        for (auto m : midi) events.push_back ({ m.samplePosition, m.getMessage() });
        size_t e = 0; int pos = 0;
        while (pos < n) {
            // next thing: an incoming event, a step, or a scheduled note-off
            int nextEv = e < events.size() ? std::max (pos, events[e].first) : n;
            int nextStep = running ? pos + (int) std::ceil (std::max (0.0, toNext)) : n;
            int nextOff = n; for (auto& o : offs) nextOff = std::min (nextOff, pos + std::max (0, (int) std::ceil (o.left)));
            for (auto& o : ons) nextOff = std::min (nextOff, pos + std::max (0, (int) std::ceil (o.left)));
            int at = std::min ({ nextEv, nextStep, nextOff, n });
            const double adv = at - pos;
            if (running) toNext -= adv;
            for (auto& o : offs) o.left -= adv;
            for (auto& o : ons) o.left -= adv;
            pos = at;
            if (pos >= n) break;
            // note-offs that are due
            for (size_t i = 0; i < offs.size();) { if (offs[i].left <= 0) { out.addEvent (juce::MidiMessage::noteOff (1, offs[i].note), pos); playingNotes.erase (std::remove (playingNotes.begin(), playingNotes.end(), offs[i].note), playingNotes.end()); offs.erase (offs.begin() + (long) i); } else ++i; }
            // swung notes that are due
            for (size_t i = 0; i < ons.size();) { if (ons[i].left <= 0) { auto o = ons[i]; ons.erase (ons.begin() + (long) i); startNote (o.note, o.vel, o.len, pos, out); } else ++i; }
            if (e < events.size() && events[e].first <= pos) {
                auto& msg = events[e].second;
                if (msg.isNoteOn()) keyDown (msg.getNoteNumber(), msg.getVelocity(), p, pos, stepSamples, out);
                else if (msg.isNoteOff()) keyUp (msg.getNoteNumber(), p, pos, out);
                else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { stopAll (pos, out); keys.clear(); out.addEvent (msg, pos); }
                else out.addEvent (msg, pos);
                ++e; continue;
            }
            if (running && toNext <= 0.5) { playStep (p, pos, stepSamples, out); toNext += stepSamples; }
        }
        midi.swapWith (out);
    }

private:
    struct Key { int note, vel; bool down; int order; };
    struct Off { int note; double left; };
    struct On { int note, vel; double left, len; };
    std::vector<On> ons;
    double sr = 48000, toNext = 0;
    std::vector<Key> keys; std::vector<Off> offs; std::vector<int> playingNotes;
    bool running = false, wasOn = false; int step = 0, count = 0;
    std::mt19937 rng { 7 };

    void keyDown (int note, int vel, const Params& p, int pos, double stepSamples, juce::MidiBuffer& out) {
        if (p.mode == 2 && std::none_of (keys.begin(), keys.end(), [] (auto& k) { return k.down; })) keys.clear(); // a fresh chord replaces the latched one
        keys.erase (std::remove_if (keys.begin(), keys.end(), [&] (auto& k) { return k.note == note; }), keys.end());
        keys.push_back ({ note, vel, true, count++ });
        if (! running) { running = true; step = 0; toNext = 0; playStep (p, pos, stepSamples, out); toNext = stepSamples; }
    }
    void keyUp (int note, const Params& p, int pos, juce::MidiBuffer& out) {
        auto it = std::find_if (keys.begin(), keys.end(), [&] (auto& k) { return k.note == note; }); if (it == keys.end()) return;
        it->down = false;
        if (p.mode != 2) keys.erase (it);
        if (keys.empty()) { running = false; stopAll (pos, out); }
    }
    void stopAll (int pos, juce::MidiBuffer& out) { for (int nn : playingNotes) out.addEvent (juce::MidiMessage::noteOff (1, nn), pos); playingNotes.clear(); offs.clear(); ons.clear(); }

    std::vector<std::pair<int, int>> sequence (const Params& p) const {
        std::vector<Key> byPitch = keys; std::sort (byPitch.begin(), byPitch.end(), [] (auto& a, auto& b) { return a.note < b.note; });
        auto octs = [&] (const std::vector<Key>& v) { std::vector<std::pair<int, int>> o; for (int k = 0; k < p.oct; ++k) for (auto& x : v) if (x.note + 12 * k <= 127) o.push_back ({ x.note + 12 * k, x.vel }); return o; };
        auto bounce = [] (std::vector<std::pair<int, int>> u) { if (u.size() > 2) for (int i = (int) u.size() - 2; i >= 1; --i) u.push_back (u[(size_t) i]); return u; };
        switch (p.pattern) {
            case 1: { auto u = octs (byPitch); std::reverse (u.begin(), u.end()); return u; }
            case 2: return bounce (octs (byPitch));
            case 3: { auto d = octs (byPitch); std::reverse (d.begin(), d.end()); return bounce (d); }
            case 4: { auto o = keys; std::sort (o.begin(), o.end(), [] (auto& a, auto& b) { return a.order < b.order; }); return octs (o); }
            default: return octs (byPitch);
        }
    }
    void playStep (const Params& p, int pos, double stepSamples, juce::MidiBuffer& out) {
        auto seq = sequence (p); if (seq.empty()) return;
        std::vector<std::pair<int, int>> notes;
        if (p.pattern == 6) notes = seq;
        else if (p.pattern == 5) notes.push_back (seq[(size_t) std::uniform_int_distribution<int> (0, (int) seq.size() - 1) (rng)]);
        else notes.push_back (seq[(size_t) (step % (int) seq.size())]);
        ++step;
        const double swing = step % 2 == 0 ? p.swing / 100.0 * stepSamples * .5 : 0; // every second step a little late
        const double len = std::max (.01 * sr, stepSamples * p.gate / 100.0);
        for (auto& [note, vel] : notes) {
            if (swing >= 1) ons.push_back ({ note, vel, swing, len });
            else startNote (note, vel, len, pos, out);
        }
    }
    void startNote (int note, int vel, double len, int pos, juce::MidiBuffer& out) {
        if (std::find (playingNotes.begin(), playingNotes.end(), note) != playingNotes.end()) {   // still sounding from the last step: restart it cleanly
            out.addEvent (juce::MidiMessage::noteOff (1, note), pos);
            offs.erase (std::remove_if (offs.begin(), offs.end(), [&] (auto& o) { return o.note == note; }), offs.end());
        } else playingNotes.push_back (note);
        out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, vel)), pos);
        offs.push_back ({ note, len });
    }
};
