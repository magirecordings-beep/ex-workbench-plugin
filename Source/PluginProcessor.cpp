#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Fx.h"

namespace {
const char* AP_LETTERS = "ABCDEFGHIJKLMNOP";
juce::String knobId (int i) { return juce::String ("ctl") + AP_LETTERS[i]; }

// banks are big: instances that use the same .exb share one copy in memory
std::mutex cacheLock;
std::map<std::string, std::weak_ptr<const exw::Bank>> bankCache;
}

juce::AudioProcessorValueTreeState::ParameterLayout ExwProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, float lo, float hi, float def, float step = 0.01f, const char* unit = "") {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step), def, AudioParameterFloatAttributes().withLabel (unit)));
    };
    f ("master", "Master", -60, 6, 0, .1f, "dB");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "keys", 1 }, "Keys", StringArray { "Preset", "Poly", "Mono", "Legato" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "glideMode", 1 }, "Glide", StringArray { "Preset", "Off", "Custom" }, 0));
    f ("glideMs", "Glide time", 5, 2000, 120, 1, "ms");
    f ("cutShift", "Cutoff shift", -9, 9, 0, .01f, "oct");
    f ("resShift", "Resonance shift", -100, 100, 0, 1, "%");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bendOwn", 1 }, "Preset bend range", true));
    f ("bendRange", "Bend range", 0, 24, 2, 1, "st");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "colOn", 1 }, "Analog", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "colMono", 1 }, "Mono bass", true));
    f ("colWarmth", "Warmth", 0, 100, 35, 1, "%");
    f ("colWeight", "Weight", 0, 100, 40, 1, "%");
    f ("colSub", "Sub", 0, 100, 25, 1, "%");
    f ("colSmooth", "Smooth", 0, 100, 20, 1, "%");
    // effects (FX A reverb, FX B, sends, master EQ)
    for (auto& d : xfx::paramDefs()) {
        if (d.choices && juce::String (d.choices) == "bool") l.add (std::make_unique<AudioParameterBool> (ParameterID { d.id, 1 }, d.name, d.def > .5f));
        else if (d.choices) l.add (std::make_unique<AudioParameterChoice> (ParameterID { d.id, 1 }, d.name, StringArray::fromTokens (d.choices, ",", ""), (int) d.def));
        else { NormalisableRange<float> r (d.lo, d.hi, d.step); if (d.skewCentre > 0) r.setSkewForCentre (d.skewCentre);
               l.add (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, d.name, r, d.def, AudioParameterFloatAttributes().withLabel (d.unit))); }
    }
    for (int i = 0; i < 16; ++i)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { knobId (i), 1 }, juce::String ("Knob ") + AP_LETTERS[i], NormalisableRange<float> (0, 127, 1), 0));
    return l;
}

ExwProcessor::ExwProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "EXW", layout()) {
    fxRack = std::make_unique<xfx::FxRack> (params);
    engine.setFxHook (fxRack.get());
    for (auto* p : getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) params.addParameterListener (r->getParameterID(), this);
    for (int i = 0; i < 16; ++i) { knobVals[(size_t) i] = 0; lastKnobParam[(size_t) i] = -1; restoreKnobs[(size_t) i] = -1; }
    pushSettings();
}

ExwProcessor::~ExwProcessor() {
    if (loader && loader->joinable()) loader->join();
}

juce::PropertiesFile& ExwProcessor::userSettings() {
    if (! props) {
        juce::PropertiesFile::Options o; o.applicationName = "Xtreme Lab"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support"; o.folderName = "Xtreme Lab";
        props = std::make_unique<juce::PropertiesFile> (o);
    }
    return *props;
}

bool ExwProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void ExwProcessor::prepareToPlay (double sampleRate, int block) {
    fxRack->prepare (sampleRate, block);
    engine.prepare (sampleRate);
    uiNotes.reset (sampleRate);
}

void ExwProcessor::parameterChanged (const juce::String&, float) { pushSettings(); }

void ExwProcessor::pushSettings() {
    auto v = [&] (const char* id) { return params.getRawParameterValue (id)->load(); };
    settings.masterDb = v ("master");
    settings.keys = (int) v ("keys"); settings.glideMode = (int) v ("glideMode"); settings.glideMs = v ("glideMs");
    settings.uniOn = false;
    settings.cutShiftOct = v ("cutShift"); settings.resShift = v ("resShift");
    settings.bendOwn = v ("bendOwn") > .5f; settings.bendRange = v ("bendRange");
    settings.colOn = v ("colOn") > .5f; settings.colMono = v ("colMono") > .5f;
    settings.colWarmth = v ("colWarmth"); settings.colWeight = v ("colWeight"); settings.colSub = v ("colSub"); settings.colSmooth = v ("colSmooth");
}

int ExwProcessor::getNumPrograms() { auto b = currentBank(); return b && ! b->presets.empty() ? (int) b->presets.size() : 1; }
const juce::String ExwProcessor::getProgramName (int index) {
    auto b = currentBank(); if (b && index >= 0 && index < (int) b->presets.size()) return juce::String::fromUTF8 (b->presets[(size_t) index].name.c_str());
    return "No bank";
}

void ExwProcessor::loadBankAsync (const juce::File& exb, int presetAfterLoad) {
    if (loading.exchange (true)) return;
    if (loader && loader->joinable()) loader->join();
    progress = 0; restorePreset = presetAfterLoad;
    { const juce::SpinLock::ScopedLockType l (bankLock); status = "Loading " + exb.getFileName() + "…"; }
    lastBankFile = exb;
    loader = std::make_unique<std::thread> ([this, exb, presetAfterLoad] {
        auto key = exb.getFullPathName().toStdString();
        std::shared_ptr<const exw::Bank> b;
        { std::lock_guard<std::mutex> g (cacheLock); if (auto it = bankCache.find (key); it != bankCache.end()) b = it->second.lock(); }
        if (! b) {
            auto nb = exw::loadBank (key, [this] (float p) { progress = p; });
            b = nb;
            if (nb->error.empty()) { std::lock_guard<std::mutex> g (cacheLock); bankCache[key] = b; }
        }
        {
            const juce::SpinLock::ScopedLockType l (bankLock);
            if (b->error.empty()) { pendingBank = b; status = juce::String::fromUTF8 (b->name.c_str()) + " · " + juce::String ((int) b->presets.size()) + " presets"; }
            else status = juce::String::fromUTF8 (b->error.c_str());
        }
        if (b->error.empty()) { bankPending = true; pendingPreset = presetAfterLoad; }
        progress = 1; loading = false; ++bankVer;
        juce::MessageManager::callAsync ([this, ok = b->error.empty(), exb] {
            if (ok) { userSettings().setValue ("recentBank", exb.getFullPathName()); updateHostDisplay (ChangeDetails().withProgramChanged (true)); }
            // a project saved while a patch was playing: bring the patch back now its presets exist
            if (ok && restorePatchOn && curPatch.isObject()) { restorePatchOn = false; setPatch (curPatch, true); }
        });
    });
}

void ExwProcessor::handleMidi (const juce::MidiMessage& m) {
    if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
    else if (m.isController()) {
        int k = learnKnob.load();
        int cc = m.getControllerNumber();
        if (k >= 0 && cc != 64 && cc < 120 && cc != 0 && cc != 32) {
            // MIDI learn: this CC now drives knob k; if another knob had it, they swap
            for (int i = 0; i < 16; ++i) if (i != k && settings.apCC[(size_t) i].load() == cc) settings.apCC[(size_t) i] = settings.apCC[(size_t) k].load();
            settings.apCC[(size_t) k] = cc; learnKnob = -1; ++learnVer;
        }
        engine.controller (cc, m.getControllerValue());
    }
    else if (m.isPitchWheel()) engine.pitchBend (m.getPitchWheelValue());
    else if (m.isChannelPressure()) engine.channelPressure (m.getChannelPressureValue());
    else if (m.isAftertouch()) engine.channelPressure (m.getAfterTouchValue());
    else if (m.isProgramChange()) { engine.programChange (m.getProgramChangeNumber()); pendingPreset = -1; }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void ExwProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // a freshly loaded bank, or a preset picked in the UI / by the host
    if (bankPending.load()) {
        const juce::SpinLock::ScopedTryLockType l (bankLock);
        if (l.isLocked() && pendingBank) { engine.setBank (pendingBank); bank = pendingBank; pendingBank.reset(); bankPending = false; }
    }
    if (int p = pendingPreset.exchange (-1); p >= 0) {
        engine.allNotesOff(); engine.setPreset (p); for (auto& v : lastKnobParam) v = -1;
        // a project being reopened: put the knobs back where they were saved
        if (restoreKnobsPending.exchange (false)) for (int i = 0; i < 16; ++i) if (restoreKnobs[(size_t) i].load() >= 0) engine.setApKnob (i, restoreKnobs[(size_t) i].load());
    }
    if (patchPending.load()) {
        const juce::SpinLock::ScopedTryLockType l (patchLock);
        if (l.isLocked()) { engine.setPatch (pendingPatch, pendingPatchReset); pendingPatchReset = false; patchPending = false; }
    }
    // host automation of the A-P knobs (only when the host actually moved one)
    for (int i = 0; i < 16; ++i) {
        float pv = params.getRawParameterValue (knobId (i))->load();
        if (lastKnobParam[(size_t) i] >= 0 && pv != lastKnobParam[(size_t) i]) engine.setApKnob (i, pv / 127.0f);
        lastKnobParam[(size_t) i] = pv;
    }
    // tempo for synced delay / flanger / phaser: the host's, else 120
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto bpm = pos->getBpm()) fxRack->setTempo (*bpm);
    juce::MidiBuffer ui; uiNotes.removeNextBlockOfMessages (ui, buffer.getNumSamples());
    for (auto meta : ui) midi.addEvent (meta.getMessage(), meta.samplePosition);

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float> tmpR; if (! R) { tmpR.resize ((size_t) buffer.getNumSamples()); R = tmpR.data(); }
    int pos = 0, n = buffer.getNumSamples();
    for (auto meta : midi) {
        int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);
    if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = .5f * (L[i] + R[i]);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    midi.clear();
    for (int i = 0; i < 16; ++i) knobVals[(size_t) i] = engine.apKnob (i);
    notes = engine.activeNotes(); players = engine.activePlayers();
    if (! patchPending.load() && patchOn.load() && ! engine.patchActive()) { patchOn = false; ++patchVer; } // a program change left the patch
}

void ExwProcessor::getStateInformation (juce::MemoryBlock& dest) {
    auto state = params.copyState();
    juce::ValueTree extra ("EXWSTATE");
    extra.setProperty ("bank", lastBankFile.getFullPathName(), nullptr);
    extra.setProperty ("preset", shownPreset(), nullptr);
    juce::String cc; for (int i = 0; i < 16; ++i) cc << settings.apCC[(size_t) i].load() << (i < 15 ? "," : "");
    extra.setProperty ("apcc", cc, nullptr);
    juce::String knobs; for (int i = 0; i < 16; ++i) knobs << juce::String (knobVals[(size_t) i].load(), 4) << (i < 15 ? "," : "");
    extra.setProperty ("knobs", knobs, nullptr);
    extra.setProperty ("patch", curPatch.isObject() ? juce::JSON::toString (curPatch, true) : juce::String(), nullptr);
    extra.setProperty ("patchOn", patchOn.load(), nullptr);
    state.removeChild (state.getChildWithName ("EXWSTATE"), nullptr);
    state.appendChild (extra, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ExwProcessor::setStateInformation (const void* data, int size) {
    auto xml = getXmlFromBinary (data, size); if (! xml) return;
    auto state = juce::ValueTree::fromXml (*xml); if (! state.isValid()) return;
    auto extra = state.getChildWithName ("EXWSTATE");
    params.replaceState (state);
    pushSettings();
    if (extra.isValid()) {
        auto cc = juce::StringArray::fromTokens (extra["apcc"].toString(), ",", "");
        for (int i = 0; i < 16 && i < cc.size(); ++i) settings.apCC[(size_t) i] = cc[i].getIntValue();
        juce::File f (extra["bank"].toString());
        int preset = (int) extra["preset"];
        auto kv = juce::StringArray::fromTokens (extra["knobs"].toString(), ",", "");
        for (int i = 0; i < 16; ++i) restoreKnobs[(size_t) i] = i < kv.size() ? kv[i].getFloatValue() : -1.0f;
        restoreKnobsPending = kv.size() == 16;
        auto pj = juce::JSON::parse (extra["patch"].toString());
        if (pj.isObject()) { curPatch = pj; restorePatchOn = (bool) extra["patchOn"]; }
        if (f.existsAsFile()) loadBankAsync (f, juce::jmax (0, preset));
        else if (extra["bank"].toString().isNotEmpty()) { const juce::SpinLock::ScopedLockType l (bankLock); status = "Bank not found: " + extra["bank"].toString(); lastBankFile = f; }
    }
}

// ---------- patches ----------
exw::Patch ExwProcessor::resolvePatch (const juce::var& p) const {
    exw::Patch out; out.active = true; out.name = p["name"].toString().toStdString();
    auto b = currentBank();
    auto* layers = p["layers"].getArray();
    for (int i = 0; i < 4; ++i) {
        auto& L = out.layers[i];
        juce::var l = layers && i < layers->size() ? (*layers)[i] : juce::var();
        if (! l.isObject()) { L.preset = -1; continue; }
        auto src = l["src"];
        L.preset = -1;
        if (b && src.isObject() && src["type"].toString() == "preset") {
            auto nm = src["name"].toString().toStdString(), bk = src["bank"].toString().toStdString();
            int any = -1;
            for (size_t k = 0; k < b->presets.size(); ++k) if (b->presets[k].name == nm) { if (any < 0) any = (int) k; if (bk.empty() || bk == b->name) { L.preset = (int) k; break; } }
            if (L.preset < 0) L.preset = any;
        }
        auto num = [&] (const juce::var& v, double d) { return v.isVoid() || v.isUndefined() ? d : (double) v; };
        L.on = (bool) num (l["on"], 1); L.solo = (bool) num (l["solo"], 0);
        L.volDb = (float) num (l["volDb"], 0); L.pan = (float) num (l["pan"], 0);
        L.coarse = (int) num (l["coarse"], 0); L.fine = (float) num (l["fine"], 0);
        L.keyLo = (int) num (l["key"]["lo"], 0); L.keyHi = (int) num (l["key"]["hi"], 127);
        L.velLo = (int) num (l["vel"]["lo"], 1); L.velHi = (int) num (l["vel"]["hi"], 127);
        L.sendA = (float) num (l["sendA"], -1); L.sendB = (float) num (l["sendB"], -1);
    }
    return out;
}

void ExwProcessor::setPatch (const juce::var& patch, bool reset) {
    curPatch = patch; patchOn = true; ++patchVer;
    auto r = resolvePatch (patch);
    const juce::SpinLock::ScopedLockType l (patchLock);
    pendingPatch = r; pendingPatchReset = reset || pendingPatchReset; patchPending = true;
}

void ExwProcessor::leavePatch() { patchOn = false; ++patchVer; pendingPreset.store (engine.presetIndex() < 0 ? 0 : engine.presetIndex()); }

juce::var ExwProcessor::patchLibrary() {
    auto v = juce::JSON::parse (userSettings().getValue ("patches"));
    return v.isArray() ? v : juce::var (juce::Array<juce::var>());
}

void ExwProcessor::storePatchLibrary (const juce::var& list) {
    userSettings().setValue ("patches", juce::JSON::toString (list, true)); userSettings().saveIfNeeded(); ++patchVer;
}

juce::AudioProcessorEditor* ExwProcessor::createEditor() { return new ExwEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ExwProcessor(); }
