#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Fx.h"
#include "Arp.h"
#include "engine/MorphTables.h"

namespace {
const char* AP_LETTERS = "ABCDEFGHIJKLMNOP";
juce::String knobId (int i) { return juce::String ("ctl") + AP_LETTERS[i]; }

// banks are big: instances that use the same .exb share one copy in memory
std::mutex cacheLock;
std::map<std::string, std::weak_ptr<const exw::Bank>> bankCache;

double num (const juce::var& v, double d) { return v.isVoid() || v.isUndefined() || v.isString() ? (v.isString() && v.toString().containsOnly ("-.0123456789") && v.toString().isNotEmpty() ? v.toString().getDoubleValue() : d) : (double) v; }
juce::var at (const juce::var& v, int i) { auto* a = v.getArray(); return a && i >= 0 && i < a->size() ? (*a)[i] : juce::var(); }
bool flag (const juce::var& v, bool d) { return v.isVoid() || v.isUndefined() ? d : (bool) v; }

exw::FiltSpec filtFrom (const juce::var& f, bool* fromPreset = nullptr) {
    exw::FiltSpec s; auto t = f["type"].toString();
    if (fromPreset) *fromPreset = ! f.isObject() || t == "preset";
    static const char* T[] = { "bypass", "lp2", "lp4", "lp6", "hp2", "hp4", "bp", "notch", "eq", "phaser", "flanger" };
    s.type = exw::F_BYPASS;
    for (int i = 0; i < 11; ++i) if (t == T[i]) s.type = i;
    for (int i = 0; i < exw::NUM_MORPHS; ++i) if (t == exw::MORPHS[i].key) { s.type = exw::F_MORPH; s.morph = i; }
    s.freq = (float) num (f["freq"], 70); s.res = (float) num (f["res"], 20); s.env = (float) num (f["env"], 0); s.decay = (float) num (f["decay"], .8);
    return s;
}
exw::EnvSpec envFrom (const juce::var& e) {
    exw::EnvSpec s; if (auto* a = e.getArray()) for (int i = 0; i < 6 && i < a->size(); ++i) { s.s[i] = (float) juce::jmax (0.0, num ((*a)[i]["s"], s.s[i])); s.l[i] = (float) num ((*a)[i]["l"], s.l[i]); }
    return s;
}
exw::LfoSpec lfoFrom (const juce::var& l, int i) {
    exw::LfoSpec s; const float rates[3] = { 5, .5f, .25f }; const int shapes[3] = { 0, 1, 6 };
    static const char* SH[] = { "sine", "triangle", "square", "saw-up", "saw-down", "random", "smooth" };
    s.shape = shapes[i % 3]; s.rate = rates[i % 3];
    if (! l.isObject()) return s;
    for (int k = 0; k < 7; ++k) if (l["shape"].toString() == SH[k]) s.shape = k;
    s.rate = (float) juce::jlimit (.01, 50.0, num (l["rate"], s.rate)); s.delay = (float) num (l["delay"], 0); s.free = l["sync"].toString() == "free";
    s.pitch = (float) num (l["pitch"], 0); s.filter = (float) num (l["filter"], 0); s.vol = (float) num (l["vol"], 0); s.pan = (float) num (l["pan"], 0);
    return s;
}
exw::FgDef fgFrom (const juce::var& f) {
    exw::FgDef d; if (! f.isObject()) return d;
    if (auto* st = f["steps"].getArray()) for (int i = 0; i < 64 && i < st->size(); ++i) d.steps[i] = juce::jlimit (-1.0f, 1.0f, (float) num ((*st)[i], 0));
    d.end = juce::jlimit (1, 64, (int) num (f["end"], 8)); d.sync = flag (f["sync"], true); d.rate = (float) num (f["rate"], 6);
    static const char* DIVS[6] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; d.div = 3; for (int i = 0; i < 6; ++i) if (f["div"].toString() == DIVS[i]) d.div = i;
    static const char* DIRS[6] = { "fwd", "rev", "pend", "rand", "brown", "once" }; d.dir = 0; for (int i = 0; i < 6; ++i) if (f["dir"].toString() == DIRS[i]) d.dir = i;
    d.smooth = flag (f["smooth"], false); d.free = f["mode"].toString() == "free";
    d.pitch = (float) num (f["pitch"], 0); d.filter = (float) num (f["filter"], 0); d.vol = (float) num (f["vol"], 0); d.pan = (float) num (f["pan"], 0);
    return d;
}

// the panel's knobs as host parameters, in the cards' own units; each acts on what its card is editing
struct PanelDef { const char* id; const char* name; float lo, hi, def, skew; const char* unit; };
const PanelDef PANEL[] = {
    { "pFiltFreq", "Filter cutoff", 0, 100, 70, 0, "%" }, { "pFiltRes", "Filter resonance", 0, 100, 20, 0, "%" },
    { "pFiltEnv", "Filter env amount", -100, 100, 0, 0, "%" }, { "pFiltDecay", "Filter env decay", .01f, 8, .8f, 1, "s" },
    { "pEnvA", "Amp attack", 0, 30, .002f, .5f, "s" }, { "pEnvD", "Amp decay", 0, 30, .6f, .8f, "s" }, { "pEnvS", "Amp sustain", 0, 100, 100, 0, "" },
    { "pEnvR", "Amp release", 0, 30, .35f, .8f, "s" }, { "pVel", "Velocity", 0, 1, .7f, 0, "" }, { "pEnvTime", "Preset envelope time", .25f, 4, 1, 1, "x" },
    { "pLfoRate", "LFO rate", .05f, 20, 5, 1, "Hz" }, { "pLfoDelay", "LFO delay", 0, 5, 0, 0, "s" }, { "pLfoPitch", "LFO to pitch", -100, 100, 0, 0, "%" },
    { "pLfoFilter", "LFO to filter", -100, 100, 0, 0, "%" }, { "pLfoVol", "LFO to volume", 0, 100, 0, 0, "%" }, { "pLfoPan", "LFO to pan", -100, 100, 0, 0, "%" },
    { "pFgRate", "FG rate", .1f, 20, 6, 2, "Hz" }, { "pFgSteps", "FG steps", 1, 64, 8, 0, "" }, { "pFgPitch", "FG to pitch", -24, 24, 0, 0, "st" },
    { "pFgFilter", "FG to filter", -100, 100, 0, 0, "%" }, { "pFgVol", "FG to volume", 0, 100, 0, 0, "%" }, { "pFgPan", "FG to pan", -100, 100, 0, 0, "%" },
    { "pLyVol", "Layer volume", -48, 6, 0, 0, "dB" }, { "pLyPan", "Layer pan", -100, 100, 0, 0, "" }, { "pLyCoarse", "Layer coarse", -36, 36, 0, 0, "st" }, { "pLyFine", "Layer fine", -100, 100, 0, 0, "ct" },
    { "pMfFreq", "Master filter cutoff", 0, 100, 70, 0, "%" }, { "pMfRes", "Master filter resonance", 0, 100, 20, 0, "%" },
    { "pMfRate", "Master filter LFO rate", .05f, 20, .2f, 1, "Hz" }, { "pMfDepth", "Master filter LFO depth", 0, 100, 0, 0, "%" },
    { "pTune", "Fine tune", -100, 100, 0, 0, "ct" }, { "pBend", "Bend range", 0, 24, 2, 0, "st" },
};
}
bool ExwProcessor::isPanelParam (const juce::String& id) { for (auto& d : PANEL) if (id == d.id) return true; return false; }

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
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "colOn", 1 }, "Analog", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "colMono", 1 }, "Mono bass", true));
    f ("colWarmth", "Warmth", 0, 100, 35, 1, "%");
    f ("colWeight", "Weight", 0, 100, 40, 1, "%");
    f ("colSub", "Sub", 0, 100, 25, 1, "%");
    f ("colSmooth", "Smooth", 0, 100, 20, 1, "%");
    // arpeggiator
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpMode", 1 }, "Arpeggiator", StringArray { "Off", "On", "Latch" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpPattern", 1 }, "Arp pattern", StringArray { "Up", "Down", "Up and down", "Down and up", "As played", "Random", "Chord repeat" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpRate", 1 }, "Arp rate", StringArray { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" }, 3));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "arpOct", 1 }, "Arp octaves", 1, 4, 1));
    f ("arpGate", "Arp gate", 5, 100, 60, 1, "%");
    f ("arpSwing", "Arp swing", 0, 75, 0, 1, "%");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpSync", 1 }, "Arp tempo", StringArray { "Host", "Own" }, 1));
    f ("arpTempo", "Arp own tempo", 40, 240, 120, 1, "BPM");
    // effects (FX A reverb, FX B, sends, master EQ)
    for (auto& d : xfx::paramDefs()) {
        if (d.choices && juce::String (d.choices) == "bool") l.add (std::make_unique<AudioParameterBool> (ParameterID { d.id, 1 }, d.name, d.def > .5f));
        else if (d.choices) l.add (std::make_unique<AudioParameterChoice> (ParameterID { d.id, 1 }, d.name, StringArray::fromTokens (d.choices, ",", ""), (int) d.def));
        else { NormalisableRange<float> r (d.lo, d.hi, d.step); if (d.skewCentre > 0) r.setSkewForCentre (d.skewCentre);
               l.add (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, d.name, r, d.def, AudioParameterFloatAttributes().withLabel (d.unit))); }
    }
    for (auto& d : PANEL) { NormalisableRange<float> r (d.lo, d.hi, 0); if (d.skew > 0) r.setSkewForCentre (d.skew);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, d.name, r, d.def, AudioParameterFloatAttributes().withLabel (d.unit))); }
    for (int i = 0; i < 16; ++i)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { knobId (i), 1 }, juce::String ("Knob ") + AP_LETTERS[i], NormalisableRange<float> (0, 127, 1), 0));
    return l;
}

ExwProcessor::ExwProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "EXW", layout()) {
    fxRack = std::make_unique<xfx::FxRack> (params);
    arp = std::make_unique<Arp>();
    engine.setFxHook (fxRack.get());
    for (auto* p : getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) params.addParameterListener (r->getParameterID(), this);
    for (int i = 0; i < 16; ++i) { knobVals[(size_t) i] = 0; uiKnobs[(size_t) i] = 0; lastKnobParam[(size_t) i] = -1; }
    peaks[0] = 0; peaks[1] = 0;
    pushSettings();
    startTimerHz (15);
}

ExwProcessor::~ExwProcessor() {
    stopTimer();
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
    arp->prepare (sampleRate);
    engine.prepare (sampleRate);
    uiNotes.reset (sampleRate);
}

void ExwProcessor::parameterChanged (const juce::String&, float) { pushSettings(); }

void ExwProcessor::pushSettings() {
    auto v = [&] (const char* id) { return params.getRawParameterValue (id)->load(); };
    settings.masterDb = v ("master");
    settings.keys = (int) v ("keys"); settings.glideMode = (int) v ("glideMode"); settings.glideMs = v ("glideMs");
    settings.colOn = v ("colOn") > .5f; settings.colMono = v ("colMono") > .5f;
    settings.colWarmth = v ("colWarmth"); settings.colWeight = v ("colWeight"); settings.colSub = v ("colSub"); settings.colSmooth = v ("colSmooth");
}

int ExwProcessor::getNumPrograms() { auto b = currentBank(); return b && ! b->presets.empty() ? (int) b->presets.size() : 1; }
const juce::String ExwProcessor::getProgramName (int index) {
    auto b = currentBank(); if (b && index >= 0 && index < (int) b->presets.size()) return juce::String::fromUTF8 (b->presets[(size_t) index].name.c_str());
    return "No bank";
}

void ExwProcessor::loadBankAsync (const juce::File& exb) {
    if (loading.exchange (true)) return;
    if (loader && loader->joinable()) loader->join();
    progress = 0;
    { const juce::SpinLock::ScopedLockType l (bankLock); status = "Loading " + exb.getFileName() + "…"; }
    lastBankFile = exb;
    loader = std::make_unique<std::thread> ([this, exb] {
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
        if (b->error.empty()) bankPending = true;
        progress = 1;
        juce::MessageManager::callAsync ([this, b, exb] {
            if (b->error.empty()) {
                // the audio thread drops the old bank's notes when it takes this one; programs made for it are rebuilt
                auto nc = std::make_shared<BankCtx>(); nc->bank = b; ctx = nc; curPreset = -1;
                userSettings().setValue ("recentBank", exb.getFullPathName()); userSettings().saveIfNeeded();
                // play what was playing (a project reopened, or the panel's choice) as soon as the bank is in
                if (engineJson.isObject()) setEngineState (engineJson, true, restorePending ? juce::var() : juce::var());
                if (restorePending) { restorePending = false; setKnobs (restoreKnobs); }
                updateHostDisplay (ChangeDetails().withProgramChanged (true));
            }
            loading = false; ++bankVer;
        });
    });
}

// ---------- the sound the panel describes ----------
const exw::Preset* ExwProcessor::resolveSource (const juce::var& src, bool& isPreset) {
    isPreset = false;
    if (! ctx || ! ctx->bank || ! src.isObject()) return nullptr;
    const auto& b = *ctx->bank;
    auto type = src["type"].toString();
    if (type == "preset") {
        auto nm = src["name"].toString().toStdString(), bk = src["bank"].toString().toStdString();
        const exw::Preset* any = nullptr;
        for (auto& p : b.presets) if (p.name == nm) { if (! any) any = &p; if (bk.empty() || bk == b.name) { isPreset = true; return &p; } }
        isPreset = any != nullptr; return any;
    }
    if (type == "zones") {
        // an instrument (a family of samples spread over the keys) or one sample: a one-voice stand-in preset
        auto* zs = src["zones"].getArray(); if (! zs || zs->isEmpty()) return nullptr;
        auto key = juce::JSON::toString (src["zones"], true).toStdString();
        auto& slot = ctx->synth[key];
        if (! slot) {
            auto p = std::make_unique<exw::Preset>(); p->name = "zones";
            exw::Voice v; v.key = { 0, 127 }; v.vel = { 0, 127 };
            for (auto& z : *zs) {
                int i = (int) num (z["i"], -1); if (i < 0 || i >= (int) b.samples.size()) continue;
                exw::Zone zz; zz.sample = i; zz.root = (int) num (z["root"], 60); zz.key = { (int) num (z["lo"], 0), (int) num (z["hi"], 127) }; zz.vel = { 0, 127 }; zz.ref = b.samples[(size_t) i].get();
                v.zones.push_back (zz);
            }
            p->voices.push_back (std::move (v));
            slot = std::move (p);
        }
        return slot.get();
    }
    return nullptr;
}

std::shared_ptr<exw::Program> ExwProcessor::buildProgram (const juce::var& s) {
    auto p = std::make_shared<exw::Program>();
    p->version = ++progVersion;
    auto P = s["P"];
    p->tune = (float) num (P["tune"], 0); p->bend = (float) num (P["bend"], 2); p->bendp = flag (P["bendp"], true); p->velsens = (float) num (P["velsens"], .7);
    p->loop = flag (P["loop"], true); p->envscale = (float) num (P["envscale"], 1); p->pmod = flag (P["pmod"], true);
    auto F = s["filt"]; p->filt = filtFrom (F); p->usePreset = flag (F["usePreset"], true);
    auto O = s["pOfs"]; p->ofsFreq = (float) num (O["freq"], 0); p->ofsRes = (float) num (O["res"], 0); p->ofsEnv = (float) num (O["env"], 0); p->ofsDecay = (float) num (O["decay"], .8);
    p->envFromPreset = s["envSource"].toString() == "preset"; p->env = envFrom (s["env"]);
    for (int k = 0; k < 3; ++k) { p->lfo[k] = lfoFrom (at (s["lfos"], k), k); p->fg[k] = fgFrom (at (s["fgs"], k)); }
    p->sendA = (float) num (s["sendA"], 20); p->sendB = (float) num (s["sendB"], 0);
    auto sel = s["sel"];
    if (sel["type"].toString() == "patch") {
        p->patch = true; p->nSlots = 4;
        auto mf = s["mf"]; p->mf = filtFrom (mf); p->mfRate = (float) num (mf["rate"], .2); p->mfDepth = (float) num (mf["depth"], 0);
        for (int i = 0; i < 4; ++i) {
            auto l = at (s["layers"], i); auto& L = p->slots[i]; L.layer = true;
            if (! l.isObject()) continue;
            L.preset = resolveSource (l["src"], L.isPreset);
            L.on = flag (l["on"], true); L.solo = flag (l["solo"], false);
            L.volDb = (float) num (l["volDb"], 0); L.pan = (float) num (l["pan"], 0); L.coarse = (int) num (l["coarse"], 0); L.fine = (float) num (l["fine"], 0);
            L.key = { (int) num (l["key"]["lo"], 0), (int) num (l["key"]["hi"], 127) }; L.vel = { (int) num (l["vel"]["lo"], 1), (int) num (l["vel"]["hi"], 127) };
            L.useSrcEnv = flag (l["useSrcEnv"], L.isPreset); L.env = envFrom (l["env"]);
            L.filt = filtFrom (l["filter"], &L.filtFromPreset);
            for (int k = 0; k < 3; ++k) { L.lfo[k] = lfoFrom (at (l["lfos"], k), k); L.fg[k] = fgFrom (at (l["fgs"], k)); }
            L.sendA = (float) num (l["sendA"], 20); L.sendB = (float) num (l["sendB"], 0);
        }
    } else if (sel.isObject()) {
        p->nSlots = 1; auto& L = p->slots[0];
        L.preset = resolveSource (sel, L.isPreset);
    }
    return p;
}

void ExwProcessor::sendProgram (std::shared_ptr<exw::Program> p, bool reset, const uint8_t* ic) {
    {
        const juce::SpinLock::ScopedLockType l (progLock);
        pendingProg = p.get(); pendingReset = pendingReset || reset;
        if (ic) { std::copy (ic, ic + 16, pendingIc.begin()); pendingHasIc = true; }
        progPending = true;
    }
    held.push_back ({ p, ctx });
    while (held.size() > 48) held.pop_front();   // the audio thread takes a new one every block; these are long done with
}

void ExwProcessor::setEngineState (const juce::var& sIn, bool reset, const juce::var& ic) {
    const juce::var s = sIn;   // (may be engineJson itself)
    engineJson = s;
    if (! ctx) return;   // no bank yet: played once it is in
    auto p = buildProgram (s);
    // the host's program number follows a single preset
    int idx = -1;
    if (p->nSlots == 1 && p->slots[0].isPreset) for (size_t i = 0; i < ctx->bank->presets.size(); ++i) if (&ctx->bank->presets[i] == p->slots[0].preset) idx = (int) i;
    if (idx != curPreset) { curPreset = idx; updateHostDisplay (ChangeDetails().withProgramChanged (true)); }
    std::array<uint8_t, 16> icv {}; bool hasIc = false;
    if (auto* a = ic.getArray(); a && a->size() >= 16) { for (int i = 0; i < 16; ++i) icv[(size_t) i] = (uint8_t) juce::jlimit (0, 127, (int) num ((*a)[i], 0)); hasIc = true; }
    sendProgram (p, reset, hasIc ? icv.data() : nullptr);
}

void ExwProcessor::selectPreset (int index, bool tellUi) {
    auto b = currentBank(); if (! b || index < 0 || index >= (int) b->presets.size()) return;
    const auto& pr = b->presets[(size_t) index];
    auto s = engineJson.isObject() ? juce::JSON::parse (juce::JSON::toString (engineJson)) : juce::var (new juce::DynamicObject());
    auto* o = s.getDynamicObject();
    auto* sel = new juce::DynamicObject(); sel->setProperty ("type", "preset"); sel->setProperty ("name", juce::String::fromUTF8 (pr.name.c_str())); sel->setProperty ("bank", juce::String::fromUTF8 (b->name.c_str()));
    o->setProperty ("sel", juce::var (sel)); o->setProperty ("envSource", "preset");
    auto* ofs = new juce::DynamicObject(); ofs->setProperty ("freq", 0); ofs->setProperty ("res", 0); ofs->setProperty ("env", 0); ofs->setProperty ("decay", .8); o->setProperty ("pOfs", juce::var (ofs));
    juce::Array<juce::var> ic; for (int i = 0; i < 16; ++i) ic.add (pr.hasIc ? (int) pr.ic[(size_t) i] : 0);
    setEngineState (s, true, ic);
    if (tellUi) selectRequest = juce::String::fromUTF8 (pr.name.c_str());
}

void ExwProcessor::setKnobs (const juce::var& values) {
    auto* a = values.getArray(); if (! a) return;
    int mask = 0;
    for (int i = 0; i < 16 && i < a->size(); ++i) { uiKnobs[(size_t) i] = (float) juce::jlimit (0.0, 1.0, num ((*a)[i], 0)); mask |= 1 << i; }
    uiKnobMask.fetch_or (mask);
}

void ExwProcessor::setApCC (const juce::var& list) {
    if (auto* a = list.getArray()) for (int i = 0; i < 16 && i < a->size(); ++i) settings.apCC[(size_t) i] = (int) num ((*a)[i], -1);
}

void ExwProcessor::timerCallback() {
    if (int pc = pendingPC.exchange (-1); pc >= 0) selectPreset (pc, true);
    // panel parameters moved by the host (automation, Ableton macros): the open panel applies them itself,
    // a closed one is stood in for here so they still change the sound
    const bool ui = uiActive.load(); bool changed = false;
    for (auto& d : PANEL) {
        float v = params.getRawParameterValue (d.id)->load();
        auto it = lastPanel.find (d.id);
        if (it == lastPanel.end()) { lastPanel[d.id] = v; continue; }
        if (it->second == v) continue;
        it->second = v;
        if (ui) continue;
        applyPanel (d.id, v); changed = true;
        if (! closedChanges) closedChanges = new juce::DynamicObject();
        closedChanges->setProperty (d.id, v);
    }
    if (changed && engineJson.isObject()) setEngineState (engineJson, false, juce::var());
}

void ExwProcessor::handleMidi (const juce::MidiMessage& m) {
    if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
    else if (m.isController()) {
        const int cc = m.getControllerNumber(), k = learnKnob.load();
        if (k >= 0 && k < 16 && cc != 64 && cc < 120 && cc != 0 && cc != 32) {
            // MIDI learn: this controller now drives knob k; a knob that had it takes k's old number
            for (int i = 0; i < 16; ++i) if (i != k && settings.apCC[(size_t) i].load() == cc) settings.apCC[(size_t) i] = settings.apCC[(size_t) k].load();
            settings.apCC[(size_t) k] = cc; learnKnob = -1; learned = k | (cc << 8);
        }
        engine.controller (cc, m.getControllerValue());
    }
    else if (m.isPitchWheel()) engine.pitchBend (m.getPitchWheelValue());
    else if (m.isChannelPressure()) engine.channelPressure (m.getChannelPressureValue());
    else if (m.isAftertouch()) engine.channelPressure (m.getAfterTouchValue());
    else if (m.isProgramChange()) { int i = engine.programChange (m.getProgramChangeNumber()); if (i >= 0) pendingPC = i; }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void ExwProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // a freshly loaded bank, then the program the panel built for it
    if (bankPending.load()) {
        const juce::SpinLock::ScopedTryLockType l (bankLock);
        if (l.isLocked() && pendingBank) { engine.setBank (pendingBank); bank = pendingBank; pendingBank.reset(); bankPending = false; }
    }
    if (progPending.load() && ! bankPending.load()) {
        const juce::SpinLock::ScopedTryLockType l (progLock);
        if (l.isLocked()) { engine.setProgram (pendingProg, pendingReset, pendingHasIc ? pendingIc.data() : nullptr); pendingReset = false; pendingHasIc = false; progPending = false; }
    }
    if (int mask = uiKnobMask.exchange (0)) for (int i = 0; i < 16; ++i) if (mask & (1 << i)) engine.setApKnob (i, uiKnobs[(size_t) i].load());
    // host automation of the A-P knobs (only when the host actually moved one)
    for (int i = 0; i < 16; ++i) {
        float pv = params.getRawParameterValue (knobId (i))->load();
        if (lastKnobParam[(size_t) i] >= 0 && pv != lastKnobParam[(size_t) i]) engine.setApKnob (i, pv / 127.0f);
        lastKnobParam[(size_t) i] = pv;
    }
    // tempo for synced delay / flanger / phaser, function generators and the arpeggiator: the host's, else 120
    double hostBpm = 120; std::optional<double> ppq; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto bpm = pos->getBpm()) { hostBpm = *bpm; fxRack->setTempo (*bpm); settings.tempo = (float) *bpm; }
        if (auto q = pos->getPpqPosition()) ppq = *q;
        playing = pos->getIsPlaying();
    }
    // the host's MIDI, for the panel's key lights, MIDI learn and wheels
    for (auto meta : midi) { const auto mm = meta.getMessage(); if (mm.getRawDataSize() <= 3) { auto* d = mm.getRawData(); midiFifo.push (d[0] | (mm.getRawDataSize() > 1 ? d[1] << 8 : 0) | (mm.getRawDataSize() > 2 ? d[2] << 16 : 0)); } }
    juce::MidiBuffer ui; uiNotes.removeNextBlockOfMessages (ui, buffer.getNumSamples());
    for (auto meta : ui) midi.addEvent (meta.getMessage(), meta.samplePosition);
    {
        auto v = [&] (const char* id) { return params.getRawParameterValue (id)->load(); };
        Arp::Params ap; ap.mode = (int) v ("arpMode"); ap.pattern = (int) v ("arpPattern"); ap.rate = (int) v ("arpRate"); ap.oct = (int) v ("arpOct");
        ap.gate = v ("arpGate"); ap.swing = v ("arpSwing"); ap.hostSync = v ("arpSync") < .5f; ap.tempo = v ("arpTempo");
        arp->process (midi, buffer.getNumSamples(), ap, hostBpm, ppq, playing);
        arpHeld = arp->heldCount();
    }

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
    float pl = 0, pr = 0; for (int i = 0; i < n; ++i) { pl = std::max (pl, std::fabs (L[i])); pr = std::max (pr, std::fabs (R[i])); }
    if (pl > peaks[0].load()) peaks[0] = pl; if (pr > peaks[1].load()) peaks[1] = pr;
    if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = .5f * (L[i] + R[i]);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    midi.clear();
    for (int i = 0; i < 16; ++i) knobVals[(size_t) i] = engine.apKnob (i);
    notes = engine.activeNotes();
}

juce::var ExwProcessor::takeClosedChanges() { juce::var v (closedChanges.get()); closedChanges = nullptr; return v; }

namespace {
juce::DynamicObject* dobj (const juce::var& v) { return v.getDynamicObject(); }
void setp (const juce::var& o, const char* k, double v) { if (auto* d = dobj (o)) d->setProperty (k, v); }
juce::var child (const juce::var& o, const char* k) {   // o[k], made an object if missing
    auto* d = dobj (o); if (! d) return {};
    if (! d->getProperty (k).isObject()) d->setProperty (k, juce::var (new juce::DynamicObject()));
    return d->getProperty (k);
}
const char* emuTypeName (int code) {
    switch (code) {
        case 127: return "bypass"; case 0: return "lp4"; case 1: return "lp2"; case 2: return "lp6"; case 8: return "hp2"; case 9: return "hp4";
        case 16: case 17: return "bp"; case 18: return "notch"; case 32: case 33: case 34: return "eq"; case 64: case 65: return "phaser";
        case 66: return "mx-phase"; case 72: return "flanger"; case 80: return "mx-ahee"; case 81: return "mx-ooah";
        default: return code >= 128 ? exw::MORPHS[(code - 128) % exw::NUM_MORPHS].key : "lp2";
    }
}
}

// the first voice of a preset source, as the panel would show it
juce::var ExwProcessor::presetEnvSec (const juce::var& src, float envscale) const {
    if (! ctx || src["type"].toString() != "preset") return {};
    for (auto& p : ctx->bank->presets) if (p.name == src["name"].toString().toStdString()) {
        if (p.voices.empty() || p.voices[0].envs.empty()) return {};
        juce::Array<juce::var> a; const auto& e = p.voices[0].envs[0];
        for (int i = 0; i < 6; ++i) { auto* o = new juce::DynamicObject(); float x = juce::jlimit (0.0f, 1.0f, e.t[i]);
            o->setProperty ("s", 163.69 * (std::pow (2.0, 11.0 * x) - 1) / 2047.0 * envscale); o->setProperty ("l", e.l[i]); a.add (juce::var (o)); }
        return a;
    }
    return {};
}
juce::var ExwProcessor::presetFilt (const juce::var& src) const {
    auto* o = new juce::DynamicObject(); o->setProperty ("type", "bypass"); o->setProperty ("freq", 70); o->setProperty ("res", 20); o->setProperty ("env", 0); o->setProperty ("decay", .8);
    if (ctx && src["type"].toString() == "preset") for (auto& p : ctx->bank->presets) if (p.name == src["name"].toString().toStdString()) {
        if (! p.voices.empty() && p.voices[0].hasFilter) { const auto& v = p.voices[0];
            o->setProperty ("type", emuTypeName (v.ftype)); o->setProperty ("freq", std::round (v.ffreq * 100)); o->setProperty ("res", std::round (v.fq * 100)); }
        break;
    }
    return juce::var (o);
}

// what moving a panel knob does to the sound description (the page's own logic, for when it's closed)
void ExwProcessor::applyPanel (const juce::String& id, float v) {
    auto& s = engineJson; if (! s.isObject()) return;
    const bool patch = s["sel"]["type"].toString() == "patch";
    const int li = juce::jlimit (0, 3, (int) num (s["edit"]["layer"], 0)), lfoI = juce::jlimit (0, 2, (int) num (s["edit"]["lfo"], 0)), fgI = juce::jlimit (0, 2, (int) num (s["edit"]["fg"], 0));
    juce::var L = patch ? at (s["layers"], li) : juce::var();
    if (patch && ! L.isObject()) return;
    auto P = child (s, "P");
    if (id.startsWith ("pFilt")) {
        const char* k = id == "pFiltFreq" ? "freq" : id == "pFiltRes" ? "res" : id == "pFiltEnv" ? "env" : "decay";
        if (patch) {
            auto f = L["filter"];
            if (! f.isObject() || f["type"].toString() == "preset") { f = presetFilt (L["src"]); dobj (L)->setProperty ("filter", f); }
            setp (f, k, v);
        } else {
            const bool presetMode = flag (s["filt"]["usePreset"], true) && s["sel"]["type"].toString() == "preset";
            if (presetMode) setp (child (s, "pOfs"), k, (! strcmp (k, "freq") || ! strcmp (k, "res")) ? (v - 50) * 2 : v);
            else setp (child (s, "filt"), k, v);
        }
    } else if (id == "pEnvA" || id == "pEnvD" || id == "pEnvR" || id == "pEnvS") {
        juce::var env;
        if (patch) {
            if (flag (L["useSrcEnv"], false)) { auto e = presetEnvSec (L["src"], (float) num (P["envscale"], 1)); if (e.isArray()) dobj (L)->setProperty ("env", e); dobj (L)->setProperty ("useSrcEnv", false); }
            env = L["env"];
        } else {
            if (s["envSource"].toString() == "preset") { auto e = presetEnvSec (s["sel"], (float) num (P["envscale"], 1)); if (e.isArray()) dobj (s)->setProperty ("env", e); }
            dobj (s)->setProperty ("envSource", s["sel"]["type"].toString() == "preset" ? "edited" : "manual");
            env = s["env"];
        }
        auto* a = env.getArray(); if (! a || a->size() < 6) return;
        auto S = [&] (int i) { return num ((*a)[i]["s"], 0); };
        if (id == "pEnvS") { setp ((*a)[3], "l", v); if (S (3) == 0) setp ((*a)[2], "l", v); }
        else { const int i = id == "pEnvA" ? 0 : id == "pEnvD" ? 2 : 4; const double cur = S (i) + S (i + 1);
               if (cur > 0) { setp ((*a)[i], "s", S (i) * v / cur); setp ((*a)[i + 1], "s", S (i + 1) * v / cur); } else setp ((*a)[i], "s", v); }
    } else if (id == "pVel") setp (P, "velsens", v);
    else if (id == "pEnvTime") setp (P, "envscale", v);
    else if (id == "pTune") setp (P, "tune", std::round (v));
    else if (id == "pBend") setp (P, "bend", std::round (v));
    else if (id.startsWith ("pLfo")) {
        auto lf = at (patch ? L["lfos"] : s["lfos"], lfoI); if (! lf.isObject()) return;
        const char* k = id == "pLfoRate" ? "rate" : id == "pLfoDelay" ? "delay" : id == "pLfoPitch" ? "pitch" : id == "pLfoFilter" ? "filter" : id == "pLfoVol" ? "vol" : "pan";
        setp (lf, k, id == "pLfoRate" || id == "pLfoDelay" ? (double) v : std::round (v));
    } else if (id.startsWith ("pFg")) {
        auto fg = at (patch ? L["fgs"] : s["fgs"], fgI); if (! fg.isObject()) return;
        const char* k = id == "pFgRate" ? "rate" : id == "pFgSteps" ? "end" : id == "pFgPitch" ? "pitch" : id == "pFgFilter" ? "filter" : id == "pFgVol" ? "vol" : "pan";
        setp (fg, k, id == "pFgRate" ? (double) v : std::round (v));
    } else if (id.startsWith ("pLy")) {
        if (! patch) return;
        if (id == "pLyVol") setp (L, "volDb", std::round (v * 2) / 2); else if (id == "pLyPan") setp (L, "pan", std::round (v));
        else if (id == "pLyCoarse") setp (L, "coarse", std::round (v)); else setp (L, "fine", std::round (v));
    } else if (id.startsWith ("pMf")) {
        if (! patch) return;
        auto mf = child (s, "mf");
        setp (mf, id == "pMfFreq" ? "freq" : id == "pMfRes" ? "res" : id == "pMfRate" ? "rate" : "depth", v);
    }
}

void ExwProcessor::getStateInformation (juce::MemoryBlock& dest) {
    auto state = params.copyState();
    juce::ValueTree extra ("EXWSTATE");
    extra.setProperty ("bank", lastBankFile.getFullPathName(), nullptr);
    extra.setProperty ("engine", engineJson.isObject() ? juce::JSON::toString (engineJson, true) : juce::String(), nullptr);
    extra.setProperty ("ui", uiJson.isObject() ? juce::JSON::toString (uiJson, true) : juce::String(), nullptr);
    juce::String cc; for (int i = 0; i < 16; ++i) cc << settings.apCC[(size_t) i].load() << (i < 15 ? "," : "");
    extra.setProperty ("apcc", cc, nullptr);
    juce::String knobs; for (int i = 0; i < 16; ++i) knobs << juce::String (knobVals[(size_t) i].load(), 4) << (i < 15 ? "," : "");
    extra.setProperty ("knobs", knobs, nullptr);
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
    if (! extra.isValid()) return;
    auto cc = juce::StringArray::fromTokens (extra["apcc"].toString(), ",", "");
    for (int i = 0; i < 16 && i < cc.size(); ++i) settings.apCC[(size_t) i] = cc[i].getIntValue();
    auto ej = juce::JSON::parse (extra["engine"].toString()); if (ej.isObject()) engineJson = ej;
    auto uj = juce::JSON::parse (extra["ui"].toString()); if (uj.isObject()) uiJson = uj;
    auto kv = juce::StringArray::fromTokens (extra["knobs"].toString(), ",", "");
    if (kv.size() == 16) { juce::Array<juce::var> a; for (auto& k : kv) a.add (k.getDoubleValue()); restoreKnobs = a; restorePending = true; }
    juce::File f (extra["bank"].toString());
    if (f.existsAsFile()) loadBankAsync (f);
    else if (extra["bank"].toString().isNotEmpty()) { const juce::SpinLock::ScopedLockType l (bankLock); status = "Bank not found: " + extra["bank"].toString(); lastBankFile = f; }
}

juce::var ExwProcessor::patchLibrary() {
    auto v = juce::JSON::parse (userSettings().getValue ("patches"));
    return v.isArray() ? v : juce::var (juce::Array<juce::var>());
}

void ExwProcessor::storePatchLibrary (const juce::var& list) {
    userSettings().setValue ("patches", juce::JSON::toString (list, true)); userSettings().saveIfNeeded();
}

juce::AudioProcessorEditor* ExwProcessor::createEditor() { return new ExwEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ExwProcessor(); }
