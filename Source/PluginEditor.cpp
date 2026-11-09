#include "PluginEditor.h"
#include "BinaryData.h"

namespace {
const char* AP_LETTERS = "ABCDEFGHIJKLMNOP";
const int AP_DEFAULT_CC[16] = { 21, 22, 23, 24, 25, 26, 27, 28, 70, 71, 72, 73, 91, 93, 82, 83 };
juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv) {
    auto* o = new juce::DynamicObject(); for (auto& p : kv) o->setProperty (p.first, p.second); return juce::var (o);
}
}

namespace {
// Windows draws the faceplate with Edge WebView2; it needs a writable folder of its own (a DAW's install folder isn't)
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
}

ExwEditor::ExwEditor (ExwProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (browserOptions()
               .withNativeIntegrationEnabled()
               .withKeepPageLoadedWhenBrowserIsHidden()
               .withResourceProvider ([] (const juce::String& url) { return resource (url); })
               .withEventListener ("exw", [this] (juce::var v) { handle (v); })) {
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (800, 480, 3200, 1920);
    getConstrainer()->setFixedAspectRatio (1600.0 / 960.0);
    setSize (1280, 768);
    startTimerHz (20);
}

ExwEditor::~ExwEditor() { stopTimer(); }

void ExwEditor::resized() { web.setBounds (getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> ExwEditor::resource (const juce::String& url) {
    auto path = url.upToFirstOccurrenceOf ("?", false, false);
    if (path == "/" || path == "/index.html" || path.isEmpty()) {
        juce::WebBrowserComponent::Resource r;
        auto* d = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        r.data.assign (d, d + BinaryData::index_htmlSize);
        r.mimeType = "text/html";
        return r;
    }
    return std::nullopt;
}

juce::StringArray ExwEditor::favourites() {
    return juce::StringArray::fromLines (proc.userSettings().getValue ("favourites"));
}

juce::var ExwEditor::presetInfo (int index) const {
    auto b = proc.currentBank(); if (! b || index < 0 || index >= (int) b->presets.size()) return {};
    const auto& pr = b->presets[(size_t) index];
    juce::Array<juce::var> labels, ic;
    for (int i = 0; i < 16; ++i) { labels.add (juce::String::fromUTF8 (pr.labels[(size_t) i].c_str())); ic.add (pr.hasIc ? (int) pr.ic[(size_t) i] : 0); }
    // which knobs the preset actually uses (so unnamed but used ones still show)
    juce::Array<juce::var> used;
    for (int i = 0; i < 16; ++i) {
        bool u = false; int src = exw::Engine::apSrc (i);
        for (auto& v : pr.voices) for (auto& c : v.cords) if (c.src == src && (c.amt != 0 || c.dst >= 168)) u = true;
        used.add (u);
    }
    int kb = 0; float glide = 0, bend = -1; int zones = 0;
    for (auto& v : pr.voices) { if (v.kb && ! kb) kb = v.kb; if (v.glide > 0 && glide == 0) glide = v.glide; if (v.bendUp >= 0 && bend < 0) bend = v.bendUp; zones += (int) v.zones.size(); }
    int filt = -1; for (auto& v : pr.voices) if (v.hasFilter) { filt = v.ftype; break; }
    return obj ({ { "index", index }, { "name", juce::String::fromUTF8 (pr.name.c_str()) }, { "voices", (int) pr.voices.size() }, { "zones", zones },
                  { "kb", kb }, { "glide", glide }, { "bend", bend }, { "filter", filt }, { "labels", labels }, { "ic", ic }, { "used", used } });
}

void ExwEditor::sendState (bool withPresets) {
    auto b = proc.currentBank();
    juce::Array<juce::var> presets;
    if (withPresets && b) for (auto& pr : b->presets) presets.add (juce::String::fromUTF8 (pr.name.c_str()));
    juce::Array<juce::var> apcc; for (int i = 0; i < 16; ++i) apcc.add (proc.settings.apCC[(size_t) i].load());
    juce::Array<juce::var> favs; for (auto& f : favourites()) if (f.isNotEmpty()) favs.add (f);
    auto* po = new juce::DynamicObject();
    for (auto* prm : proc.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) { float v = r->convertFrom0to1 (r->getValue()); po->setProperty (r->getParameterID(), v); sentParams[r->getParameterID()] = v; }
    auto recent = juce::File (proc.userSettings().getValue ("recentBank"));
    auto msg = obj ({ { "type", "state" }, { "bank", b ? juce::var (juce::String::fromUTF8 (b->name.c_str())) : juce::var() }, { "status", proc.bankStatus() },
                      { "loading", proc.isLoading() }, { "recent", recent.existsAsFile() ? recent.getFileName() : juce::String() },
                      { "missing", b ? b->missingZones : 0 }, { "preset", proc.shownPreset() }, { "info", presetInfo (proc.shownPreset()) },
                      { "params", juce::var (po) }, { "apcc", apcc }, { "favs", favs } });
    juce::Array<juce::var> knobs; for (int i = 0; i < 16; ++i) { knobs.add (proc.knob (i)); sentKnobs[i] = proc.knob (i); }
    msg.getDynamicObject()->setProperty ("knobs", knobs);
    if (withPresets) msg.getDynamicObject()->setProperty ("presets", presets);
    // patches: the one playing (if any) and the saved library's names
    msg.getDynamicObject()->setProperty ("patch", proc.currentPatch());
    msg.getDynamicObject()->setProperty ("patchOn", proc.patchPlaying());
    juce::Array<juce::var> names; if (auto* a = proc.patchLibrary().getArray()) for (auto& p : *a) names.add (p["name"]);
    msg.getDynamicObject()->setProperty ("patches", names);
    send (msg);
    sentBankVer = proc.bankVersion(); sentPreset = proc.shownPreset(); sentLearn = proc.learnedVersion(); sentPatchVer = proc.patchVersion();
}

void ExwEditor::timerCallback() {
    if (! ready) return;
    if (proc.bankVersion() != sentBankVer) { sendState (true); return; }
    if (proc.shownPreset() != sentPreset || proc.learnedVersion() != sentLearn || proc.patchVersion() != sentPatchVer) { sendState (false); return; }
    // live status: notes, knob positions (MIDI moves them), loading progress, parameters the host changed
    juce::Array<juce::var> knobs; bool knobsMoved = false;
    for (int i = 0; i < 16; ++i) { float k = proc.knob (i); knobs.add (k); if (std::abs (k - sentKnobs[i]) > 1e-4f) { knobsMoved = true; sentKnobs[i] = k; } }
    auto* changed = new juce::DynamicObject(); bool any = false;
    for (auto* prm : proc.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) {
        float v = r->convertFrom0to1 (r->getValue()); auto id = r->getParameterID();
        if (sentParams[id] != v) { sentParams[id] = v; changed->setProperty (id, v); any = true; }
    }
    auto msg = obj ({ { "type", "status" }, { "notes", proc.notesPlaying() }, { "players", proc.playersPlaying() }, { "arp", proc.arpKeys() }, { "loading", proc.isLoading() }, { "progress", proc.loadProgress() }, { "status", proc.bankStatus() } });
    if (knobsMoved) msg.getDynamicObject()->setProperty ("knobs", knobs);
    if (any) msg.getDynamicObject()->setProperty ("params", juce::var (changed)); else delete changed;
    send (msg);
}

void ExwEditor::handle (const juce::var& m) {
    auto type = m["type"].toString();
    if (type == "ready") { ready = true; sendState (true); }
    else if (type == "loadBank") {
        auto start = proc.lastBankFile.existsAsFile() ? proc.lastBankFile.getParentDirectory() : juce::File (proc.userSettings().getValue ("recentBank")).getParentDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Choose an Emulator X bank (.exb). Its SamplePool is found next to it automatically.", start, "*.exb");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f.existsAsFile()) proc.loadBankAsync (f, 0);
        });
    }
    else if (type == "loadRecent") { juce::File f (proc.userSettings().getValue ("recentBank")); if (f.existsAsFile()) proc.loadBankAsync (f, 0); }
    else if (type == "preset") proc.selectPreset ((int) m["index"]);
    else if (type == "param") {
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (proc.params.getParameter (m["id"].toString()))) {
            float v = (float) (double) m["value"];
            r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (v)); r->endChangeGesture();
            sentParams[r->getParameterID()] = r->convertFrom0to1 (r->getValue());
        }
    }
    else if (type == "patch") { proc.setPatch (m["patch"], (bool) m["reset"]); sentPatchVer = proc.patchVersion(); } // the UI already shows it
    else if (type == "leavePatch") proc.leavePatch();
    else if (type == "patchSave") {
        // store under its name (replacing one with the same name)
        auto lib = proc.patchLibrary(); auto* a = lib.getArray(); auto p = m["patch"]; auto nm = p["name"].toString();
        int at = -1; for (int i = 0; i < a->size(); ++i) if ((*a)[i]["name"].toString() == nm) at = i;
        if (at >= 0) a->set (at, p); else a->add (p);
        proc.storePatchLibrary (lib);
    }
    else if (type == "patchLoad") { auto lib = proc.patchLibrary(); int i = (int) m["index"]; if (auto* a = lib.getArray(); a && i >= 0 && i < a->size()) proc.setPatch ((*a)[i], true); }
    else if (type == "patchDelete") { auto lib = proc.patchLibrary(); int i = (int) m["index"]; if (auto* a = lib.getArray(); a && i >= 0 && i < a->size()) { a->remove (i); proc.storePatchLibrary (lib); } }
    else if (type == "patchImport") {
        chooser = std::make_unique<juce::FileChooser> ("Open Xtreme Lab patches (.exwp)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.exwp;*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc) {
            auto lib = proc.patchLibrary(); auto* a = lib.getArray(); int n = 0;
            for (auto& f : fc.getResults()) {
                auto j = juce::JSON::parse (f.loadFileAsString());
                if (auto* ps = j["patches"].getArray()) for (auto& p : *ps) if (p.isObject()) { a->add (p); ++n; }
            }
            if (n) { proc.storePatchLibrary (lib); send (obj ({ { "type", "toast" }, { "text", "Added " + juce::String (n) + " patch" + (n > 1 ? "es." : ".") } })); }
            else if (fc.getResults().size()) send (obj ({ { "type", "toast" }, { "text", "No Xtreme Lab patches in that file." } }));
        });
    }
    else if (type == "patchExport") {
        chooser = std::make_unique<juce::FileChooser> ("Save patches", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Xtreme Lab patches.exwp"), "*.exwp");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            auto out = obj ({ { "format", "ex-workbench-patch" }, { "version", 1 }, { "patches", proc.patchLibrary() } });
            f.withFileExtension (".exwp").replaceWithText (juce::JSON::toString (out));
            send (obj ({ { "type", "toast" }, { "text", "Saved " + f.withFileExtension (".exwp").getFileName() } }));
        });
    }
    else if (type == "note") proc.queueNote ((int) m["note"], (int) m["vel"]);
    else if (type == "bend") proc.queueBend ((int) m["value"]);
    else if (type == "cc") proc.queueCC ((int) m["cc"], (int) m["value"]);
    else if (type == "learn") proc.startLearn ((int) m["index"]);
    else if (type == "defcc") { for (int i = 0; i < 16; ++i) proc.settings.apCC[(size_t) i] = AP_DEFAULT_CC[i]; sendState (false); }
    else if (type == "fav") {
        auto favs = favourites(); auto name = m["name"].toString();
        if (favs.contains (name)) favs.removeString (name); else favs.add (name);
        favs.removeEmptyStrings(); proc.userSettings().setValue ("favourites", favs.joinIntoString ("\n")); proc.userSettings().saveIfNeeded();
        sendState (false);
    }
    juce::ignoreUnused (AP_LETTERS);
}
