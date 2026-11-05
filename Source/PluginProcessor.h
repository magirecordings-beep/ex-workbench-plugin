#pragma once
#include <JuceHeader.h>
#include "engine/Engine.h"

// EX Workbench as a plugin: an Emulator X bank player with the browser version's engine.
class ExwProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    ExwProcessor();
    ~ExwProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "EX Workbench"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    // the host's program list = the bank's presets
    int getNumPrograms() override;
    int getCurrentProgram() override { return engine.presetIndex() < 0 ? 0 : engine.presetIndex(); }
    void setCurrentProgram (int index) override { selectPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    void loadBankAsync (const juce::File& exb, int presetAfterLoad = 0);
    void selectPreset (int index) { pendingPreset.store (index); }
    void queueNote (int note, int velocity) { uiNotes.addMessageToQueue (velocity > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity) : juce::MidiMessage::noteOff (1, note)); }
    void startLearn (int knob) { learnKnob.store (knob); }
    std::shared_ptr<const exw::Bank> currentBank() const { const juce::SpinLock::ScopedLockType l (bankLock); return bank; }
    juce::String bankStatus() const { const juce::SpinLock::ScopedLockType l (bankLock); return status; }
    float loadProgress() const { return progress.load(); }
    bool isLoading() const { return loading.load(); }
    int shownPreset() const { int p = pendingPreset.load(); return p >= 0 ? p : engine.presetIndex(); }
    float knob (int i) const { return knobVals[(size_t) i].load(); }
    int notesPlaying() const { return notes.load(); }
    int playersPlaying() const { return players.load(); }
    int bankVersion() const { return bankVer.load(); }
    int learnedVersion() const { return learnVer.load(); }

    juce::AudioProcessorValueTreeState params;
    exw::Settings settings;
    juce::File lastBankFile;
    juce::PropertiesFile& userSettings();

private:
    exw::Engine engine { settings };
    mutable juce::SpinLock bankLock;
    std::shared_ptr<const exw::Bank> bank, pendingBank;
    std::atomic<bool> bankPending { false }, loading { false };
    std::atomic<int> pendingPreset { -1 }, bankVer { 0 }, notes { 0 }, players { 0 }, learnKnob { -1 }, learnVer { 0 };
    std::atomic<float> progress { 0 };
    juce::String status;
    std::array<std::atomic<float>, 16> knobVals;
    std::array<float, 16> lastKnobParam;
    juce::MidiMessageCollector uiNotes;
    std::unique_ptr<std::thread> loader;
    std::unique_ptr<juce::PropertiesFile> props;
    int restorePreset = -1;
    std::array<std::atomic<float>, 16> restoreKnobs; std::atomic<bool> restoreKnobsPending { false };

    void parameterChanged (const juce::String& id, float value) override;
    void pushSettings();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleMidi (const juce::MidiMessage& m);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExwProcessor)
};
