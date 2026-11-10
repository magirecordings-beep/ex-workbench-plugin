#include "PluginEditor.h"
#include "BinaryData.h"

namespace {
juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv) {
    auto* o = new juce::DynamicObject(); for (auto& p : kv) o->setProperty (p.first, p.second); return juce::var (o);
}
// Windows draws the panel with Edge WebView2; it needs a writable folder of its own (a DAW's install folder isn't)
juce::WebBrowserComponent::Options browserOptions() {
    juce::WebBrowserComponent::Options o;
   #if JUCE_WINDOWS
    o = o.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
         .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                      .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Xtreme Lab").getChildFile ("WebView2"))
                                      .withStatusBarDisabled()
                                      .withBackgroundColour (juce::Colour (0xff1b1d20)));
   #endif
    return o;
}
juce::WebBrowserComponent::Resource bytes (const void* d, size_t n, const char* mime) {
    juce::WebBrowserComponent::Resource r; auto* b = reinterpret_cast<const std::byte*> (d); r.data.assign (b, b + n); r.mimeType = mime; return r;
}
}

ExwEditor::ExwEditor (ExwProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (browserOptions()
               .withNativeIntegrationEnabled()
               .withKeepPageLoadedWhenBrowserIsHidden()
               .withResourceProvider ([this] (const juce::String& url) { return resource (url); })
               .withEventListener ("exw", [this] (juce::var v) { handle (v); })) {
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (800, 480, 3200, 1920);
    getConstrainer()->setFixedAspectRatio (1600.0 / 960.0);
    setSize (1280, 768);
    startTimerHz (25);
}

ExwEditor::~ExwEditor() { stopTimer(); }

void ExwEditor::resized() { web.setBounds (getLocalBounds()); }

// the page, and the bank it plays: the .exb itself (the page reads its presets with its own parser),
// the SamplePool's samples (names, rates, loops) and their audio for the waveform display and exports
std::optional<juce::WebBrowserComponent::Resource> ExwEditor::resource (const juce::String& url) {
    auto path = url.upToFirstOccurrenceOf ("?", false, false);
    if (path == "/" || path == "/index.html" || path.isEmpty()) {
        // the page's own remembered settings go in with it, so they are there before its first line runs
        auto html = juce::String::fromUTF8 (BinaryData::index_html, BinaryData::index_htmlSize);
        html = html.replace ("/*EXW_LS*/{}", juce::JSON::toString (lsGet(), true).replace ("</", "<\\/"));
        return bytes (html.toRawUTF8(), html.getNumBytesAsUTF8(), "text/html");
    }
    auto b = proc.currentBank();
    if (path == "/x/exb") {
        juce::MemoryBlock mb; if (! b || ! juce::File (juce::String::fromUTF8 (b->path.c_str())).loadFileAsData (mb)) return std::nullopt;
        return bytes (mb.getData(), mb.getSize(), "application/octet-stream");
    }
    if (path == "/x/samples.json") {
        if (! b) return std::nullopt;
        // which sample-list number each pool sample answers to (so the page resolves zones the same way)
        std::map<const exw::Sample*, int> sl;
        for (auto& pr : b->presets) for (auto& v : pr.voices) for (auto& z : v.zones) if (z.ref && ! sl.count (z.ref)) sl[z.ref] = z.sample;
        juce::Array<juce::var> list;
        for (size_t i = 0; i < b->samples.size(); ++i) {
            auto& s = *b->samples[i];
            auto* o = new juce::DynamicObject();
            o->setProperty ("i", (int) i); o->setProperty ("name", juce::String::fromUTF8 (s.name.c_str())); o->setProperty ("rate", s.rate); o->setProperty ("channels", s.channels);
            o->setProperty ("frames", s.frames); o->setProperty ("loop", s.loop); o->setProperty ("loopStart", s.loopStart); o->setProperty ("loopEnd", s.loopEnd);
            if (auto it = sl.find (&s); it != sl.end()) o->setProperty ("sl", it->second);
            list.add (juce::var (o));
        }
        auto j = juce::JSON::toString (juce::var (list), true);
        return bytes (j.toRawUTF8(), j.getNumBytesAsUTF8(), "application/json");
    }
    if (path.startsWith ("/x/pcm/")) {
        int i = path.fromLastOccurrenceOf ("/", false, false).getIntValue();
        if (! b || i < 0 || i >= (int) b->samples.size()) return std::nullopt;
        auto& s = *b->samples[(size_t) i];
        // 16-bit, channels one after the other
        std::vector<int16_t> d (s.L); if (s.channels == 2 && ! s.R.empty()) d.insert (d.end(), s.R.begin(), s.R.end());
        return bytes (d.data(), d.size() * 2, "application/octet-stream");
    }
    return std::nullopt;
}

juce::var ExwEditor::bankInfo() const {
    auto b = proc.currentBank(); if (! b) return {};
    return obj ({ { "name", juce::String::fromUTF8 (b->name.c_str()) }, { "path", juce::String::fromUTF8 (b->path.c_str()) }, { "ver", proc.bankVersion() }, { "missing", b->missingZones }, { "samples", (int) b->samples.size() } });
}

juce::var ExwEditor::lsGet() { auto v = juce::JSON::parse (proc.userSettings().getValue ("ls")); return v.isObject() ? v : juce::var (new juce::DynamicObject()); }
void ExwEditor::lsPut (const juce::var& o) { proc.userSettings().setValue ("ls", juce::JSON::toString (o, true)); proc.userSettings().saveIfNeeded(); }

void ExwEditor::sendInit() {
    juce::Array<juce::var> apcc; for (int i = 0; i < 16; ++i) apcc.add (proc.settings.apCC[(size_t) i].load());
    juce::Array<juce::var> knobs; for (int i = 0; i < 16; ++i) { knobs.add (proc.knob (i)); sentKnobs[i] = proc.knob (i); }
    auto* po = new juce::DynamicObject();
    for (auto* prm : proc.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) po->setProperty (r->getParameterID(), r->convertFrom0to1 (r->getValue()));
    auto recent = juce::File (proc.userSettings().getValue ("recentBank"));
    send (obj ({ { "type", "init" }, { "ui", proc.uiState() }, { "engine", proc.engineState() }, { "ls", lsGet() }, { "patches", proc.patchLibrary() },
                 { "apcc", apcc }, { "knobs", knobs }, { "params", juce::var (po) }, { "bank", bankInfo() }, { "loading", proc.isLoading() }, { "status", proc.bankStatus() },
                 { "recent", recent.existsAsFile() ? recent.getFullPathName() : juce::String() } }));
    sentBankVer = proc.bankVersion();
}

void ExwEditor::timerCallback() {
    if (! ready) return;
    if (proc.bankVersion() != sentBankVer && ! proc.isLoading()) { sentBankVer = proc.bankVersion(); send (obj ({ { "type", "bank" }, { "bank", bankInfo() }, { "status", proc.bankStatus() } })); }
    juce::Array<juce::var> knobs; bool moved = false;
    for (int i = 0; i < 16; ++i) { float k = proc.knob (i); knobs.add (k); if (std::abs (k - sentKnobs[i]) > 1e-4f) { moved = true; sentKnobs[i] = k; } }
    juce::Array<juce::var> midi; proc.midiForUi (midi);
    auto msg = obj ({ { "type", "status" }, { "notes", proc.notesPlaying() }, { "arp", proc.arpKeys() }, { "peak", juce::Array<juce::var> { proc.peak (0), proc.peak (1) } } });
    auto* o = msg.getDynamicObject();
    if (moved) o->setProperty ("knobs", knobs);
    if (midi.size()) o->setProperty ("midi", midi);
    if (proc.isLoading() || sentLoading || proc.bankStatus() != sentStatus) { o->setProperty ("loading", proc.isLoading()); o->setProperty ("progress", proc.loadProgress()); o->setProperty ("status", proc.bankStatus()); sentStatus = proc.bankStatus(); sentLoading = proc.isLoading(); }
    auto sel = proc.takeSelectRequest(); if (sel.isNotEmpty()) o->setProperty ("select", sel);
    send (msg);
}

void ExwEditor::handle (const juce::var& m) {
    auto type = m["type"].toString();
    if (type == "ready") { ready = true; sendInit(); }
    else if (type == "engine") proc.setEngineState (m["state"], (bool) m["reset"], m["ic"]);
    else if (type == "ui") proc.setUiState (m["state"]);
    else if (type == "knobs") proc.setKnobs (m["values"]);
    else if (type == "apcc") proc.setApCC (m["list"]);
    else if (type == "param") {
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (proc.params.getParameter (m["id"].toString()))) {
            float v = (float) (double) m["value"];
            if (std::abs (r->convertFrom0to1 (r->getValue()) - v) > 1e-6f) { r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (v)); r->endChangeGesture(); }
        }
    }
    else if (type == "note") { int n = juce::jlimit (0, 127, (int) m["note"]), v = (int) m["vel"]; proc.queue (v > 0 ? juce::MidiMessage::noteOn (1, n, (juce::uint8) juce::jlimit (1, 127, v)) : juce::MidiMessage::noteOff (1, n)); }
    else if (type == "midi") { auto* d = m["data"].getArray(); if (d && d->size() == 3) proc.queue (juce::MidiMessage ((int) (*d)[0], (int) (*d)[1], (int) (*d)[2])); }
    else if (type == "panic") proc.queue (juce::MidiMessage::allNotesOff (1));
    else if (type == "openBank" || type == "openFiles") {
        auto recent = juce::File (proc.userSettings().getValue ("recentBank"));
        auto start = proc.lastBankFile.existsAsFile() ? proc.lastBankFile.getParentDirectory() : recent.existsAsFile() ? recent.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const bool files = type == "openFiles";
        chooser = std::make_unique<juce::FileChooser> (files ? "Open an Emulator X bank (.exb) or Xtreme Lab patches (.exwp)" : "Open an Emulator X bank (.exb). Its SamplePool is found next to it automatically.",
                                                       start, files ? "*.exb;*.exwp;*.json" : "*.exb");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | (files ? juce::FileBrowserComponent::canSelectMultipleItems : 0), [this] (const juce::FileChooser& fc) {
            for (auto& f : fc.getResults()) {
                if (f.hasFileExtension ("exb")) proc.loadBankAsync (f);
                else if (f.existsAsFile()) send (obj ({ { "type", "patchFile" }, { "name", f.getFileNameWithoutExtension() }, { "text", f.loadFileAsString() } }));
            }
        });
    }
    else if (type == "loadRecent") { juce::File f (proc.userSettings().getValue ("recentBank")); if (f.existsAsFile()) proc.loadBankAsync (f); }
    else if (type == "forget") { proc.userSettings().removeValue ("recentBank"); proc.userSettings().saveIfNeeded(); }
    else if (type == "lsSet") { auto o = lsGet(); o.getDynamicObject()->setProperty (m["k"].toString(), m["v"].toString()); lsPut (o); }
    else if (type == "lsDel") { auto o = lsGet(); o.getDynamicObject()->removeProperty (m["k"].toString()); lsPut (o); }
    else if (type == "patchesStore") proc.storePatchLibrary (m["patches"]);
    else if (type == "saveFile") {
        // a file the page made (WAV, ZIP, SFZ bank, patches), arriving in base64 pieces; the last one asks where to save it
        if ((int) m["i"] == 0) { saveBuf.reset(); saveName = m["name"].toString(); }
        juce::MemoryOutputStream mo (saveBuf, true); juce::Base64::convertFromBase64 (mo, m["chunk"].toString()); mo.flush();
        if ((int) m["i"] == (int) m["n"] - 1) {
            auto dir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
            auto ext = saveName.fromLastOccurrenceOf (".", false, false);
            chooser = std::make_unique<juce::FileChooser> ("Save " + saveName, dir.getChildFile (saveName), ext.isNotEmpty() ? "*." + ext : "*");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (f == juce::File()) return;
                if (f.replaceWithData (saveBuf.getData(), saveBuf.getSize())) send (obj ({ { "type", "toast" }, { "text", "Saved " + f.getFileName() } }));
                else send (obj ({ { "type", "toast" }, { "text", "Couldn’t save " + f.getFileName() } }));
                saveBuf.reset();
            });
        }
    }
}
