#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// The plugin window: the Xtreme Lab faceplate (HTML, like the browser version) in a web view,
// talking to the processor through JUCE's native bridge ("exw" events both ways).
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
    int sentBankVer = -1, sentPreset = -2, sentLearn = -1;
    float sentKnobs[16] {};
    std::map<juce::String, float> sentParams;

    static std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);
    void handle (const juce::var& msg);
    void send (const juce::var& v) { web.emitEventIfBrowserIsVisible ("exw", v); }
    void sendState (bool withPresets);
    juce::var presetInfo (int index) const;
    juce::StringArray favourites();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExwEditor)
};
