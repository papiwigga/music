#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/AutotuneEngine.h"

class AfinaProcessor : public juce::AudioProcessor
{
public:
    AfinaProcessor();
    ~AfinaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const char* noteName (int pitchClass);

    juce::AudioProcessorValueTreeState apvts;

    /** Pitch information for the editor's display (0 = unvoiced). Read from the UI thread only. */
    using PitchFrame = afina::AutotuneEngine::Frame;
    bool popFrame (PitchFrame& frame);

private:
    void pushFrame (const PitchFrame& frame);
    void configureEngine();

    afina::AutotuneEngine engine;
    int engineChannels = 0;
    int configuredRange = -1;
    double currentSampleRate = 44100.0;

    std::atomic<float>* retuneParam = nullptr;
    std::atomic<float>* humanizeParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* refAParam = nullptr;
    std::atomic<float>* rangeParam = nullptr;
    std::array<std::atomic<float>*, 12> noteParams {};

    static constexpr int fifoSize = 1024;
    juce::AbstractFifo fifo { fifoSize };
    std::array<PitchFrame, fifoSize> frames {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfinaProcessor)
};
