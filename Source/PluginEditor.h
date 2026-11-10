#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// The plugin window: the browser version of Xtreme Lab itself in a web view. Its sound engine is replaced by the
// processor's: the page describes what to play ("engine" events), the processor serves it the bank and samples.
class ExwEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ExwEditor (ExwProcessor&);
    ~ExwEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0c0d0f)); }

private:
    ExwProcessor& proc;
    juce::WebBrowserComponent web;
    std::unique_ptr<juce::FileChooser> chooser;
    bool ready = false;
    int sentBankVer = -1; float sentKnobs[16] {}; juce::String sentStatus; bool sentLoading = false;
    juce::MemoryBlock saveBuf; juce::String saveName;

    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);
    void handle (const juce::var& msg);
    void send (const juce::var& v) { web.emitEventIfBrowserIsVisible ("exw", v); }
    void sendInit();
    juce::var bankInfo() const;
    juce::var lsGet();
    void lsPut (const juce::var& o);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExwEditor)
};
