#pragma once
#include <JuceHeader.h>
#include "engine/Engine.h"
#include <deque>
namespace xfx { class FxRack; }
class Arp;

// Xtreme Lab as a plugin. The panel is the browser version itself; it describes the sound it wants (the selected
// preset, instrument or sample, or a patch of four layers with all their edits) as JSON, and the engine here plays it.
class ExwProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::Timer {
public:
    ExwProcessor();
    ~ExwProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Xtreme Lab"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    // the host's program list = the bank's presets
    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, curPreset); }
    void setCurrentProgram (int index) override { selectPreset (index, true); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    void loadBankAsync (const juce::File& exb);
    // the sound the panel describes (see ui bridge: "engine" message). reset: a new sound (held notes are let go)
    void setEngineState (const juce::var& s, bool reset, const juce::var& ic);
    juce::var engineState() const { return engineJson; }
    void setUiState (const juce::var& s) { uiJson = s; }
    juce::var uiState() const { return uiJson; }
    void selectPreset (int index, bool tellUi);   // a host or MIDI program change, or from the panel
    juce::String takeSelectRequest() { auto s = selectRequest; selectRequest = {}; return s; }
    void setKnobs (const juce::var& values);      // A-P from the panel (0..1)
    void setApCC (const juce::var& list);
    // on-screen keys, pitch and mod wheels
    void queue (const juce::MidiMessage& m) { uiNotes.addMessageToQueue (m); }
    std::shared_ptr<const exw::Bank> currentBank() const { const juce::SpinLock::ScopedLockType l (bankLock); return bank ? bank : pendingBank; }
    juce::String bankStatus() const { const juce::SpinLock::ScopedLockType l (bankLock); return status; }
    float loadProgress() const { return progress.load(); }
    bool isLoading() const { return loading.load(); }
    float knob (int i) const { return knobVals[(size_t) i].load(); }
    int notesPlaying() const { return notes.load(); }
    int arpKeys() const { return arpHeld.load(); }
    int bankVersion() const { return bankVer.load(); }
    float peak (int ch) { return peaks[(size_t) ch].exchange (0); }
    // MIDI that came in from the host, for the panel (key lights, learn, wheels): packed status | d1 << 8 | d2 << 16
    void midiForUi (juce::Array<juce::var>& out) { int v; while (midiFifo.pop (v)) out.add (v); }
    juce::var patchLibrary();
    void storePatchLibrary (const juce::var& list);

    juce::AudioProcessorValueTreeState params;
    exw::Settings settings;
    juce::File lastBankFile;
    juce::PropertiesFile& userSettings();

private:
    struct BankCtx {   // a bank plus the stand-in presets made for instruments and samples played from it
        std::shared_ptr<const exw::Bank> bank;
        std::map<std::string, std::unique_ptr<exw::Preset>> synth;
    };
    exw::Engine engine { settings };
    std::unique_ptr<xfx::FxRack> fxRack;
    std::unique_ptr<Arp> arp;
    mutable juce::SpinLock bankLock;
    std::shared_ptr<const exw::Bank> bank, pendingBank;
    std::shared_ptr<BankCtx> ctx;   // message thread
    std::atomic<bool> bankPending { false }, loading { false };
    std::atomic<int> arpHeld { 0 }, bankVer { 0 }, notes { 0 }, pendingPC { -1 };
    std::atomic<float> progress { 0 };
    std::array<std::atomic<float>, 2> peaks;
    juce::String status;
    std::array<std::atomic<float>, 16> knobVals, uiKnobs; std::atomic<int> uiKnobMask { 0 };
    std::array<float, 16> lastKnobParam;
    juce::MidiMessageCollector uiNotes;
    std::unique_ptr<std::thread> loader;
    std::unique_ptr<juce::PropertiesFile> props;
    // programs: built here, handed to the audio thread, kept alive for a while after it moves on
    juce::SpinLock progLock; const exw::Program* pendingProg = nullptr; bool pendingReset = false; std::array<uint8_t, 16> pendingIc {}; bool pendingHasIc = false;
    std::atomic<bool> progPending { false };
    std::deque<std::pair<std::shared_ptr<exw::Program>, std::shared_ptr<BankCtx>>> held;
    int progVersion = 0, curPreset = -1;
    juce::var engineJson, uiJson; bool restorePending = false; juce::var restoreKnobs;
    juce::String selectRequest;
    struct IntFifo { juce::AbstractFifo f { 512 }; int buf[512] {};
        void push (int v) { int s1, n1, s2, n2; f.prepareToWrite (1, s1, n1, s2, n2); if (n1) buf[s1] = v; else if (n2) buf[s2] = v; f.finishedWrite (n1 + n2); }
        bool pop (int& v) { int s1, n1, s2, n2; f.prepareToRead (1, s1, n1, s2, n2); if (! (n1 + n2)) return false; v = n1 ? buf[s1] : buf[s2]; f.finishedRead (n1 + n2); return true; } } midiFifo;

    std::shared_ptr<exw::Program> buildProgram (const juce::var& s);
    const exw::Preset* resolveSource (const juce::var& src, bool& isPreset);
    void sendProgram (std::shared_ptr<exw::Program> p, bool reset, const uint8_t* ic);
    void parameterChanged (const juce::String& id, float value) override;
    void timerCallback() override;
    void pushSettings();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleMidi (const juce::MidiMessage& m);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExwProcessor)
};
