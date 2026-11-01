#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

bool isInCMajor (int pitchClass)
{
    return pitchClass == 0 || pitchClass == 2 || pitchClass == 4 || pitchClass == 5
        || pitchClass == 7 || pitchClass == 9 || pitchClass == 11;
}
} // namespace

const char* AfinaProcessor::noteName (int pitchClass)
{
    return kNoteNames[((pitchClass % 12) + 12) % 12];
}

juce::AudioProcessorValueTreeState::ParameterLayout AfinaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "retune", 1 }, "Retune Speed",
        NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.4f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "humanize", 1 }, "Humanize",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "amount", 1 }, "Amount",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "refa", 1 }, "Reference A",
        NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "range", 1 }, "Voice Range",
        StringArray { "Low voice (90 Hz+)", "Medium voice (130 Hz+)", "High voice (180 Hz+)" }, 1));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "note" + String (i), 1 }, String ("Note ") + kNoteNames[i], isInCMajor (i)));

    return layout;
}

AfinaProcessor::AfinaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    retuneParam = apvts.getRawParameterValue ("retune");
    humanizeParam = apvts.getRawParameterValue ("humanize");
    amountParam = apvts.getRawParameterValue ("amount");
    mixParam = apvts.getRawParameterValue ("mix");
    refAParam = apvts.getRawParameterValue ("refa");
    rangeParam = apvts.getRawParameterValue ("range");

    for (int i = 0; i < 12; ++i)
        noteParams[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i));
}

bool AfinaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void AfinaProcessor::configureEngine()
{
    static const float lowestHz[3] = { 90.0f, 130.0f, 180.0f };
    const int range = juce::jlimit (0, 2, (int) rangeParam->load());
    engine.prepare (currentSampleRate, engineChannels, lowestHz[range]);
    setLatencySamples (engine.latency());
    configuredRange = range;
}

void AfinaProcessor::prepareToPlay (double sampleRate, int)
{
    engineChannels = juce::jlimit (1, afina::AutotuneEngine::maxChannels, getTotalNumInputChannels());
    currentSampleRate = sampleRate;
    configureEngine();

    fifo.reset();
}

void AfinaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels != engineChannels || numChannels < 1)
        return;

    if ((int) rangeParam->load() != configuredRange)
        configureEngine();   // voice range changed: re-prepare (brief, one-off) and report the new latency

    afina::PitchCorrector::Params params;
    for (size_t i = 0; i < 12; ++i)
        params.notes[i] = noteParams[i]->load() > 0.5f;
    params.retuneMs = retuneParam->load();
    params.humanize = humanizeParam->load() * 0.01f;
    params.amount = amountParam->load() * 0.01f;
    params.refA = refAParam->load();
    engine.setParams (params, mixParam->load() * 0.01f);

    const float* in[afina::AutotuneEngine::maxChannels] = {};
    float* out[afina::AutotuneEngine::maxChannels] = {};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        in[ch] = buffer.getReadPointer (ch);
        out[ch] = buffer.getWritePointer (ch);
    }

    engine.process (in, out, numSamples, [this] (const PitchFrame& f) { pushFrame (f); });
}

void AfinaProcessor::pushFrame (const PitchFrame& frame)
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        frames[(size_t) start1] = frame;
        fifo.finishedWrite (1);
    }
}

bool AfinaProcessor::popFrame (PitchFrame& frame)
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        frame = frames[(size_t) start1];
        fifo.finishedRead (1);
        return true;
    }
    return false;
}

juce::AudioProcessorEditor* AfinaProcessor::createEditor()
{
    return new AfinaEditor (*this);
}

void AfinaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AfinaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfinaProcessor();
}
