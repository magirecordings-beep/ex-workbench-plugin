// Drives the real plugin processor (no host, no UI): loads the bank from saved state, plays notes, checks the output.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Arp.h"
#include <cmath>
#include <cstdio>
#include <thread>
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
static double rms (juce::AudioBuffer<float>& b) { double e = 0; for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) e += b.getSample (c, i) * b.getSample (c, i); return std::sqrt (e / (b.getNumChannels() * b.getNumSamples())); }
int main (int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto* proc = dynamic_cast<ExwProcessor*> (p.get());
    p->setPlayConfigDetails (0, 2, 48000, 512); p->prepareToPlay (48000, 512);
    juce::String bank = argc > 1 ? argv[1] : "/tmp/claude-0/pool/x/Xtreme_Lead-1.exb";
    proc->loadBankAsync (juce::File (bank), 2);
    for (int k = 0; k < 600 && (proc->isLoading() || ! proc->currentBank()); ++k) { juce::Thread::sleep (20); 
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; p->processBlock (b, m); }
    auto b = proc->currentBank();
    printf ("status: %s | presets %d | program %d '%s'\n", proc->bankStatus().toRawUTF8(), p->getNumPrograms(), p->getCurrentProgram(), p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    auto play = [&] (const char* label, std::vector<int> notes) {
        juce::AudioBuffer<float> buf (2, 512); double tot = 0; int blocks = 90;
        for (int k = 0; k < blocks; ++k) {
            juce::MidiBuffer m; buf.clear();
            if (k == 0) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 10);
            if (k == 60) for (int n : notes) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            p->processBlock (buf, m); tot += rms (buf);
        }
        printf ("%-34s avg rms %.4f  notes now %d\n", label, tot / blocks, proc->notesPlaying());
    };
    play ("preset 3 chord", { 48, 55, 60 });
    p->setCurrentProgram (0); { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; p->processBlock (x, m); }
    printf ("after setCurrentProgram(0): %d '%s'\n", p->getCurrentProgram(), p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    play ("preset 1 single", { 60 });
    proc->params.getParameter ("cutShift")->setValueNotifyingHost (0.3f);
    play ("preset 1 cutoff shifted down", { 60 });
    // program change via MIDI
    { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::programChange (1, 9), 0); p->processBlock (x, m); }
    printf ("after MIDI program change 10: %d '%s'\n", p->getCurrentProgram(), p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    // knob via CC21 then save/restore state into a new instance
    { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 99), 0); p->processBlock (x, m); }
    juce::MemoryBlock mb; p->getStateInformation (mb);
    printf ("state %zu bytes, knob A now %.3f\n", mb.getSize(), proc->knob (0));
    std::unique_ptr<juce::AudioProcessor> p2 (createPluginFilter()); auto* proc2 = dynamic_cast<ExwProcessor*> (p2.get());
    p2->setPlayConfigDetails (0, 2, 48000, 512); p2->prepareToPlay (48000, 512);
    p2->setStateInformation (mb.getData(), (int) mb.getSize());
    for (int k = 0; k < 600 && (proc2->isLoading() || ! proc2->currentBank()); ++k) { juce::Thread::sleep (20); }
    for (int k = 0; k < 3; ++k) { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; p2->processBlock (x, m); }
    printf ("restored: program %d '%s' cutShift %.2f oct knob A %.3f (shared bank: %s)\n", p2->getCurrentProgram(), p2->getProgramName (p2->getCurrentProgram()).toRawUTF8(),
            proc2->params.getRawParameterValue ("cutShift")->load(), proc2->knob (0), proc2->currentBank() == proc->currentBank() ? "yes" : "no");
    // effects: reverb tail, each FX B type as a send, drive as an insert
    auto setp = [&] (const char* id, float v) { auto* r = dynamic_cast<juce::RangedAudioParameter*> (proc->params.getParameter (id)); r->setValueNotifyingHost (r->convertTo0to1 (v)); };
    auto tail = [&] (const char* label) {
        juce::AudioBuffer<float> buf (2, 512); double on = 0, after = 0; int nan = 0;
        for (int k = 0; k < 150; ++k) {
            juce::MidiBuffer m; buf.clear();
            if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (k == 40) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            p->processBlock (buf, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (! std::isfinite (buf.getSample (c, i))) ++nan;
            if (k < 40) on += rms (buf); else if (k >= 70) after += rms (buf);
        }
        printf ("%-34s while held %.4f  0.6-1.6 s after release %.5f  nan %d\n", label, on / 40, after / 80, nan);
    };
    p->setCurrentProgram (2); { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; p->processBlock (x, m); }
    setp ("sendA", 0); setp ("sendB", 0); tail ("dry (no sends)");
    setp ("sendA", 60); tail ("reverb send 60%");
    setp ("sendA", 0); setp ("sendB", 60);
    const char* names[] = { "chorus", "flanger", "phaser", "stereo delay", "drive" };
    for (int t = 0; t < 5; ++t) { setp ("fxbType", (float) t); tail ((juce::String ("FX B send: ") + names[t]).toRawUTF8()); }
    setp ("sendB", 0); setp ("fxbIns", 1); setp ("fxbType", 4); setp ("drDrive", 8); tail ("FX B insert: drive 8");
    setp ("fxbIns", 0); setp ("sendA", 20);
    { // arpeggiator on its own: hold C-E-G for 2 s at 120 BPM in 1/16, up, 2 octaves, swing 50
        Arp a; a.prepare (48000); Arp::Params ap; ap.mode = 1; ap.rate = 3; ap.oct = 2; ap.swing = 50; ap.gate = 50;
        int ons = 0, offs = 0; std::vector<int> seq; std::vector<int> times; int t = 0;
        for (int k = 0; k < 188; ++k) {
            juce::MidiBuffer m;
            if (k == 0) for (int n : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 100);
            if (k == 187) for (int n : { 48, 52, 55 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            a.process (m, 512, ap, 120, std::nullopt, false);
            for (auto e : m) { auto mm = e.getMessage(); if (mm.isNoteOn()) { ++ons; if (seq.size() < 8) { seq.push_back (mm.getNoteNumber()); times.push_back (t + e.samplePosition); } } else if (mm.isNoteOff()) ++offs; }
            t += 512;
        }
        printf ("arp: %d note-ons, %d note-offs in 2 s (expect ~16); first notes", ons, offs);
        for (size_t i = 0; i < seq.size(); ++i) printf (" %d@%.0fms", seq[i], times[i] / 48.0);
        printf ("\n");
    }
    { // mod: own envelope, LFO 1 square on volume, FG 1 major arp on pitch - block levels should wobble
        std::vector<double> base;
        auto wob = [&] (const char* label) {
            juce::AudioBuffer<float> buf (2, 512); std::vector<double> r; int nan = 0;
            for (int k = 0; k < 60; ++k) { juce::MidiBuffer m; buf.clear(); if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); if (k == 59) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                p->processBlock (buf, m); r.push_back (rms (buf)); for (int i = 0; i < 512; ++i) if (! std::isfinite (buf.getSample (0, i))) ++nan; }
            int turns = 0; for (size_t i = 6; i + 1 < 58; ++i) if ((r[i] - r[i - 1]) * (r[i + 1] - r[i]) < 0 && std::fabs (r[i + 1] - r[i]) > .02 * r[i]) ++turns;
            printf ("%-34s level turns %d  blocks 2-8:", label, turns); for (int i = 2; i < 9; ++i) printf (" %.3f", r[(size_t) i]); printf ("  nan %d\n", nan);
            if (base.empty()) base = r; else { printf ("   vs none, every 4th block:"); for (size_t i = 4; i < 60; i += 4) printf (" %.2f", r[i] / std::max (1e-6, base[i])); printf ("\n"); }
            for (int k = 0; k < 120; ++k) { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; p->processBlock (x, m); }
        };
        p->setCurrentProgram (0); { juce::AudioBuffer<float> x (2, 512); juce::MidiBuffer m; p->processBlock (x, m); }
        setp ("sendA", 0); setp ("cutShift", 0); wob ("no modulation");
        setp ("lfo1Shape", 2); setp ("lfo1Rate", 3); setp ("lfo1Vol", 100); wob ("LFO 1 square 3 Hz on volume");
        setp ("lfo1Vol", 0); setp ("envMode", 1); setp ("envA", 0.4f); wob ("own envelope, 0.4 s attack");
        setp ("envMode", 0);
        auto fg = juce::JSON::parse (R"([{"steps":[0,0.5,-0.5,1,0,0,0,0],"end":4,"sync":1,"div":"1/16","dir":"fwd","mode":"key","vol":100,"pitch":12}])");
        proc->setFgs (fg); wob ("FG 1 steps on volume + pitch");
        juce::MemoryBlock mb2; p->getStateInformation (mb2);
        auto x2 = juce::AudioProcessor::getXmlFromBinary (mb2.getData(), (int) mb2.getSize());
        printf ("state keeps FGs: %s\n", x2 && x2->toString().contains ("steps") ? "yes" : "NO");
        proc->setFgs (juce::JSON::parse ("[]")); setp ("sendA", 20);
    }
    // timing: how long 10 s of an 8-note chord takes to render

    auto t0 = juce::Time::getMillisecondCounterHiRes(); juce::AudioBuffer<float> buf (2, 512);
    for (int k = 0; k < 940; ++k) { juce::MidiBuffer m; buf.clear(); if (k == 0) for (int n : { 36, 43, 48, 55, 60, 64, 67, 72 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0); p->processBlock (buf, m); }
    printf ("10 s of 8-note chord rendered in %.0f ms (%.1f%% of real time, one core)\n", juce::Time::getMillisecondCounterHiRes() - t0, (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    p2.reset(); p.reset();
    return 0;
}
