#include "PluginEditor.h"

namespace
{
const juce::Colour kBackground { 0xff14171c };
const juce::Colour kPanel { 0xff1d2128 };
const juce::Colour kAccent { 0xff3ddc97 };
const juce::Colour kInputTrace { 0xff8a93a0 };
const juce::Colour kText { 0xffd5dae1 };

struct ScalePreset
{
    const char* name;
    std::vector<int> intervals;
};

const std::vector<ScalePreset>& scalePresets()
{
    static const std::vector<ScalePreset> presets {
        { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Major pentatonic", { 0, 2, 4, 7, 9 } },
        { "Minor pentatonic", { 0, 3, 5, 7, 10 } },
        { "Blues", { 0, 3, 5, 6, 7, 10 } },
    };
    return presets;
}

bool hasInterval (const std::vector<int>& v, int x)
{
    return std::find (v.begin(), v.end(), x) != v.end();
}

bool isBlackKey (int pitchClass)
{
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}
} // namespace

// ------------------------------------------------------------------------------------------
// PitchView
// ------------------------------------------------------------------------------------------

void PitchView::addFrame (const AfinaProcessor::PitchFrame& frame)
{
    history.push_back (frame);
    while (history.size() > maxFrames)
        history.pop_front();
}

void PitchView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 8.0f);

    // Follow the singer: centre the display on the recent average pitch.
    float sum = 0.0f;
    int count = 0;
    for (size_t i = history.size() > 150 ? history.size() - 150 : 0; i < history.size(); ++i)
        if (history[i].inMidi > 0.0f)
        {
            sum += history[i].inMidi;
            ++count;
        }
    if (count > 0)
        centre += (sum / (float) count - centre) * 0.05f;

    const float span = 24.0f;
    const float lo = centre - span * 0.5f;
    const float hi = centre + span * 0.5f;
    const float leftMargin = 34.0f;
    const auto area = bounds.reduced (6.0f).withTrimmedLeft (leftMargin);

    auto yOf = [&] (float midi) { return area.getBottom() - (midi - lo) / (hi - lo) * area.getHeight(); };

    g.saveState();
    g.reduceClipRegion (bounds.reduced (2.0f).toNearestInt());

    // Note lanes.
    g.setFont (juce::FontOptions (11.0f));
    for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (hi) + 1; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const bool allowed = processor.apvts.getRawParameterValue ("note" + juce::String (pc))->load() > 0.5f;
        const float top = yOf ((float) n + 0.5f);
        const float bottom = yOf ((float) n - 0.5f);

        if (allowed)
        {
            g.setColour (kAccent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (area.getX(), top, area.getWidth(), bottom - top));
            g.setColour (kAccent.withAlpha (0.25f));
            g.drawHorizontalLine ((int) yOf ((float) n), area.getX(), area.getRight());
            g.setColour (kText.withAlpha (0.75f));
            g.drawText (juce::String (AfinaProcessor::noteName (pc)) + juce::String (n / 12 - 1),
                        juce::Rectangle<float> (bounds.getX() + 4.0f, top, leftMargin, bottom - top).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
        else if (pc == 0)
        {
            g.setColour (kText.withAlpha (0.12f));
            g.drawHorizontalLine ((int) yOf ((float) n), area.getX(), area.getRight());
        }
    }

    // Traces.
    auto drawTrace = [&] (bool corrected, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool open = false;
        const float step = area.getWidth() / (float) maxFrames;
        const float x0 = area.getRight() - step * (float) history.size();

        for (size_t i = 0; i < history.size(); ++i)
        {
            const float m = corrected ? history[i].outMidi : history[i].inMidi;
            const float x = x0 + step * (float) i;
            if (m > 0.0f)
            {
                if (! open)
                    path.startNewSubPath (x, yOf (m));
                else
                    path.lineTo (x, yOf (m));
                open = true;
            }
            else
            {
                open = false;
            }
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawTrace (false, kInputTrace, 1.5f);
    drawTrace (true, kAccent, 2.5f);

    g.restoreState();
}

// ------------------------------------------------------------------------------------------
// Editor
// ------------------------------------------------------------------------------------------

void AfinaEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kText);
    l.attachToComponent (&s, false);
    addAndMakeVisible (l);
}

AfinaEditor::AfinaEditor (AfinaProcessor& p)
    : AudioProcessorEditor (&p), processor (p), pitchView (p)
{
    setSize (760, 500);

    addAndMakeVisible (pitchView);

    // Key / scale.
    for (int i = 0; i < 12; ++i)
        keyBox.addItem (AfinaProcessor::noteName (i), i + 1);
    scaleBox.addItem ("Custom", 1);
    for (size_t i = 0; i < scalePresets().size(); ++i)
        scaleBox.addItem (scalePresets()[i].name, (int) i + 2);

    keyBox.setSelectedId (1, juce::dontSendNotification);
    scaleBox.setSelectedId (2, juce::dontSendNotification);
    keyBox.onChange = [this] { applyScale(); };
    scaleBox.onChange = [this] { applyScale(); };
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("range")))
        rangeBox.addItemList (choice->choices, 1);
    addAndMakeVisible (rangeBox);
    rangeAtt = std::make_unique<ComboAttachment> (processor.apvts, "range", rangeBox);

    keyLabel.setText ("Key", juce::dontSendNotification);
    scaleLabel.setText ("Scale", juce::dontSendNotification);
    rangeLabel.setText ("Voice", juce::dontSendNotification);
    for (auto* l : { &keyLabel, &scaleLabel, &rangeLabel })
    {
        l->setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (l);
    }
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setColour (juce::Label::textColourId, kText.withAlpha (0.6f));
    addAndMakeVisible (latencyLabel);

    // Note buttons.
    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteButtons[(size_t) i];
        b.setButtonText (AfinaProcessor::noteName (i));
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, isBlackKey (i) ? juce::Colour (0xff0f1115) : juce::Colour (0xff2b313b));
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId, kText);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this] { scaleBox.setSelectedId (1, juce::dontSendNotification); };
        addAndMakeVisible (b);
        noteAttachments[(size_t) i] = std::make_unique<ButtonAttachment> (processor.apvts, "note" + juce::String (i), b);
    }

    // Knobs.
    setupKnob (retuneSlider, retuneLabel, "Retune Speed");
    setupKnob (humanizeSlider, humanizeLabel, "Humanize");
    setupKnob (amountSlider, amountLabel, "Amount");
    setupKnob (mixSlider, mixLabel, "Mix");
    setupKnob (refASlider, refALabel, "Reference A");
    retuneAtt = std::make_unique<SliderAttachment> (processor.apvts, "retune", retuneSlider);
    humanizeAtt = std::make_unique<SliderAttachment> (processor.apvts, "humanize", humanizeSlider);
    amountAtt = std::make_unique<SliderAttachment> (processor.apvts, "amount", amountSlider);
    mixAtt = std::make_unique<SliderAttachment> (processor.apvts, "mix", mixSlider);
    refAAtt = std::make_unique<SliderAttachment> (processor.apvts, "refa", refASlider);

    syncCombosFromNotes();

    const double sr = processor.getSampleRate();
    if (sr > 0.0)
        latencyLabel.setText (juce::String (processor.getLatencySamples() * 1000.0 / sr, 0) + " ms latency",
                              juce::dontSendNotification);

    startTimerHz (30);
}

void AfinaEditor::applyScale()
{
    const int key = keyBox.getSelectedId() - 1;
    const int scale = scaleBox.getSelectedId() - 2;
    if (key < 0 || scale < 0)
        return;   // "Custom": the note buttons are in charge

    const auto& intervals = scalePresets()[(size_t) scale].intervals;
    for (int i = 0; i < 12; ++i)
    {
        const bool allowed = hasInterval (intervals, ((i - key) % 12 + 12) % 12);
        if (auto* prm = processor.apvts.getParameter ("note" + juce::String (i)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (allowed ? 1.0f : 0.0f);
            prm->endChangeGesture();
        }
    }
}

void AfinaEditor::syncCombosFromNotes()
{
    std::vector<int> current;
    for (int i = 0; i < 12; ++i)
        if (processor.apvts.getRawParameterValue ("note" + juce::String (i))->load() > 0.5f)
            current.push_back (i);

    for (int key = 0; key < 12; ++key)
        for (size_t s = 0; s < scalePresets().size(); ++s)
        {
            std::vector<int> expected;
            for (int i = 0; i < 12; ++i)
                if (hasInterval (scalePresets()[s].intervals, ((i - key) % 12 + 12) % 12))
                    expected.push_back (i);

            if (expected == current)
            {
                keyBox.setSelectedId (key + 1, juce::dontSendNotification);
                scaleBox.setSelectedId ((int) s + 2, juce::dontSendNotification);
                return;
            }
        }

    scaleBox.setSelectedId (1, juce::dontSendNotification);
}

void AfinaEditor::timerCallback()
{
    AfinaProcessor::PitchFrame f;
    while (processor.popFrame (f))
        pitchView.addFrame (f);
    pitchView.repaint();

    const double sr = processor.getSampleRate();
    if (sr > 0.0)
        latencyLabel.setText (juce::String (processor.getLatencySamples() * 1000.0 / sr, 0) + " ms latency",
                              juce::dontSendNotification);
}

void AfinaEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("AfinaVST", 16, 8, 300, 30, juce::Justification::centredLeft);
}

void AfinaEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (30);
    latencyLabel.setBounds (header.removeFromRight (200));
    area.removeFromTop (6);

    pitchView.setBounds (area.removeFromTop (190));
    area.removeFromTop (12);

    auto scaleRow = area.removeFromTop (28);
    keyLabel.setBounds (scaleRow.removeFromLeft (34));
    keyBox.setBounds (scaleRow.removeFromLeft (70));
    scaleRow.removeFromLeft (16);
    scaleLabel.setBounds (scaleRow.removeFromLeft (46));
    scaleBox.setBounds (scaleRow.removeFromLeft (150));
    scaleRow.removeFromLeft (16);
    rangeLabel.setBounds (scaleRow.removeFromLeft (42));
    rangeBox.setBounds (scaleRow.removeFromLeft (190));

    area.removeFromTop (8);
    auto noteRow = area.removeFromTop (40);
    const int w = noteRow.getWidth() / 12;
    for (auto& b : noteButtons)
        b.setBounds (noteRow.removeFromLeft (w).reduced (2));

    area.removeFromTop (34);   // room for the knob labels
    auto knobs = area.removeFromTop (110);
    const int kw = knobs.getWidth() / 5;
    for (auto* s : { &retuneSlider, &humanizeSlider, &amountSlider, &mixSlider, &refASlider })
        s->setBounds (knobs.removeFromLeft (kw).reduced (10, 0));
}
