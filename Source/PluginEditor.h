#pragma once

#include "PluginProcessor.h"

#include <deque>

/** Scrolling pitch display: grey = detected pitch, green = corrected pitch.
    Lanes for the notes allowed by the current scale are highlighted. */
class PitchView : public juce::Component
{
public:
    explicit PitchView (AfinaProcessor& p) : processor (p) {}

    void addFrame (const AfinaProcessor::PitchFrame& frame);
    void paint (juce::Graphics& g) override;

private:
    static constexpr size_t maxFrames = 700;   // ~2.8 s at 4 ms per frame

    AfinaProcessor& processor;
    std::deque<AfinaProcessor::PitchFrame> history;
    float centre = 57.0f;   // MIDI note at the middle of the display
};

class AfinaEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit AfinaEditor (AfinaProcessor&);
    ~AfinaEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void applyScale();
    void syncCombosFromNotes();
    void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text);

    AfinaProcessor& processor;

    PitchView pitchView;
    juce::ComboBox keyBox, scaleBox, rangeBox;
    juce::Label keyLabel, scaleLabel, rangeLabel, latencyLabel;
    std::unique_ptr<ComboAttachment> rangeAtt;
    std::array<juce::TextButton, 12> noteButtons;
    std::array<std::unique_ptr<ButtonAttachment>, 12> noteAttachments;

    juce::Slider retuneSlider, humanizeSlider, amountSlider, mixSlider, refASlider;
    juce::Label retuneLabel, humanizeLabel, amountLabel, mixLabel, refALabel;
    std::unique_ptr<SliderAttachment> retuneAtt, humanizeAtt, amountAtt, mixAtt, refAAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfinaEditor)
};
