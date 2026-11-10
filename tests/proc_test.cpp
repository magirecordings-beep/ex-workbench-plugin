// Drives the real plugin processor (no host, no UI): loads a bank, gives it sound descriptions the way the panel
// does (JSON), plays notes and checks the output, then saves and restores the state into a new instance.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
static double rms (juce::AudioBuffer<float>& b) { double e = 0; for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) e += b.getSample (c, i) * b.getSample (c, i); return std::sqrt (e / (b.getNumChannels() * b.getNumSamples())); }
static void pump (int ms) { auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms; while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
int main (int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto* proc = dynamic_cast<ExwProcessor*> (p.get());
    p->setPlayConfigDetails (0, 2, 48000, 512); p->prepareToPlay (48000, 512);
    juce::String bank = argc > 1 ? argv[1] : "/tmp/claude-0/pool/x/Xtreme_Lead-1.exb";
    auto block = [&] (ExwProcessor* pr, juce::MidiBuffer& m) { juce::AudioBuffer<float> b (2, 512); pr->processBlock (b, m); return b; };
    proc->loadBankAsync (juce::File (bank));
    for (int k = 0; k < 600 && (proc->isLoading() || ! proc->currentBank()); ++k) { pump (20); juce::MidiBuffer m; block (proc, m); }
    printf ("status: %s | programs %d\n", proc->bankStatus().toRawUTF8(), p->getNumPrograms());
    auto J = [] (const char* s) { auto v = juce::JSON::parse (juce::String (s)); if (! v.isObject()) printf ("BAD JSON\n"); return v; };
    auto play = [&] (const char* label, std::vector<int> notes) {
        double tot = 0, d = 0, e = 0; int blocks = 80, nan = 0;
        for (int k = 0; k < blocks; ++k) {
            juce::MidiBuffer m;
            if (k == 0) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 10);
            if (k == 50) for (int n : notes) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            auto b = block (proc, m); tot += rms (b);
            for (int i = 1; i < 512; ++i) { float x = b.getSample (0, i); if (! std::isfinite (x)) ++nan; d += (x - b.getSample (0, i - 1)) * (x - b.getSample (0, i - 1)); e += x * x; }
        }
        printf ("%-40s avg rms %.4f  brightness %.3f  nan %d\n", label, tot / blocks, std::sqrt (d / (e + 1e-12)), nan);
        for (int k = 0; k < 120; ++k) { juce::MidiBuffer m; block (proc, m); }
    };
    const char* common = R"("P":{"tune":0,"bend":2,"bendp":true,"velsens":0.7,"loop":true,"envscale":1,"pmod":true},"filt":{"type":"bypass","freq":70,"res":20,"env":0,"decay":0.8,"usePreset":true},"pOfs":{"freq":0,"res":0,"env":0,"decay":0.8},"envSource":"preset","env":[{"s":0.002,"l":100},{"s":0,"l":100},{"s":0.6,"l":100},{"s":0,"l":100},{"s":0.35,"l":0},{"s":0,"l":0}],"sendA":0,"sendB":0)";
    auto state = [&] (juce::String sel, juce::String extra = {}) { return J ((juce::String ("{") + common + ",\"sel\":" + sel + extra + "}").toRawUTF8()); };
    proc->setEngineState (state (R"({"type":"preset","name":"raw:Basic Saw","bank":"Xtreme_Lead-1.exb"})"), true, juce::var()); pump (50);
    printf ("host program now %d '%s'\n", p->getCurrentProgram(), p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    play ("preset raw:Basic Saw", { 48 });
    proc->setEngineState (state (R"({"type":"preset","name":"raw:Basic Saw","bank":"Xtreme_Lead-1.exb"})").toString().isEmpty() ? juce::var() :
        J ((juce::String ("{") + common + R"(,"sel":{"type":"preset","name":"raw:Basic Saw","bank":"Xtreme_Lead-1.exb"},"pOfs":{"freq":-40,"res":0,"env":0,"decay":0.8}})").toRawUTF8()), false, juce::var()); pump (50);
    play ("  cutoff shift -40 (darker)", { 48 });
    proc->setEngineState (state (R"({"type":"zones","zones":[{"i":3,"root":55,"lo":0,"hi":127}]})"), true, juce::var()); pump (50);
    play ("one sample (zones)", { 55 });
    juce::String layers = R"(,"layers":[{"src":{"type":"preset","name":"raw:Basic Saw","bank":"Xtreme_Lead-1.exb"},"on":true,"solo":false,"volDb":0,"pan":0,"coarse":0,"fine":0,"key":{"lo":0,"hi":127},"vel":{"lo":1,"hi":127},"useSrcEnv":true,"filter":{"type":"%F%","freq":25,"res":30,"env":0,"decay":0.8},"lfos":[{"shape":"square","rate":4,"delay":0,"sync":"key","pitch":0,"filter":0,"vol":%V%,"pan":0}],"fgs":[],"sendA":0,"sendB":0},null,null,null],"mf":{"type":"bypass","freq":70,"res":20,"rate":0.2,"depth":0})";
    proc->setEngineState (state (R"({"type":"patch"})", layers.replace ("%F%", "preset").replace ("%V%", "0")), true, juce::var()); pump (50);
    play ("patch: layer 1 as its preset", { 48 });
    proc->setEngineState (state (R"({"type":"patch"})", layers.replace ("%F%", "lp4").replace ("%V%", "0")), false, juce::var()); pump (50);
    play ("patch: layer 1 own 4-pole lowpass (darker)", { 48 });
    proc->setEngineState (state (R"({"type":"patch"})", layers.replace ("%F%", "mx-ahee").replace ("%V%", "100")), false, juce::var()); pump (50);
    play ("patch: layer 1 morph + LFO square on volume", { 48 });
    // MIDI program change -> the processor picks that preset itself
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::programChange (1, 9), 0); block (proc, m); pump (200); }
    printf ("after MIDI program change 10: %d '%s'\n", p->getCurrentProgram(), p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    play ("  plays it", { 60 });
    // save and restore into a new instance
    juce::MemoryBlock mb; p->getStateInformation (mb);
    std::unique_ptr<juce::AudioProcessor> p2 (createPluginFilter()); auto* proc2 = dynamic_cast<ExwProcessor*> (p2.get());
    p2->setPlayConfigDetails (0, 2, 48000, 512); p2->prepareToPlay (48000, 512);
    p2->setStateInformation (mb.getData(), (int) mb.getSize());
    for (int k = 0; k < 600 && (proc2->isLoading() || ! proc2->currentBank()); ++k) { pump (20); juce::MidiBuffer m; block (proc2, m); }
    pump (100); { juce::MidiBuffer m; block (proc2, m); }
    double t = 0; for (int k = 0; k < 30; ++k) { juce::MidiBuffer m; if (! k) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); auto b = block (proc2, m); t += rms (b); }
    printf ("restored instance: program %d '%s', plays rms %.4f (state %zu bytes)\n", p2->getCurrentProgram(), p2->getProgramName (p2->getCurrentProgram()).toRawUTF8(), t / 30, mb.getSize());
    // timing: 10 s of an 8-note chord through a two-layer patch
    proc->setEngineState (state (R"({"type":"patch"})", layers.replace ("%F%", "lp4").replace ("%V%", "0").replace ("null,null,null", R"({"src":{"type":"preset","name":"pad:Strings Box","bank":"Xtreme_Lead-1.exb"},"on":true,"filter":{"type":"preset"},"lfos":[],"fgs":[],"sendA":30,"sendB":0},null,null)")), true, juce::var()); pump (50);
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int k = 0; k < 940; ++k) { juce::MidiBuffer m; if (k == 0) for (int n : { 36, 43, 48, 55, 60, 64, 67, 72 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0); block (proc, m); }
    printf ("10 s of 8 notes x 2 layers: %.0f ms (%.1f%% of real time, one core)\n", juce::Time::getMillisecondCounterHiRes() - t0, (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    p2.reset(); p.reset();
    return 0;
}
