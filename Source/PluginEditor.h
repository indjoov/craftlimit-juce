#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─── Custom Look and Feel ────────────────────────────────────────────────────
class CraftLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CraftLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider  (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;

    juce::Colour accentOrange  { 0xFFFF6B35 };
    juce::Colour accentBlue    { 0xFF06B6D4 };
    juce::Colour accentGreen   { 0xFF22C55E };
    juce::Colour accentPurple  { 0xFF7C3AED };
    juce::Colour accentYellow  { 0xFFF59E0B };
    juce::Colour bgDark        { 0xFF0A0A14 };
    juce::Colour bgMid         { 0xFF14142A };
    juce::Colour bgPanel       { 0xFF0D0D1A };
};

// ─── Segmented Level Meter ───────────────────────────────────────────────────
class LevelMeter : public juce::Component, public juce::Timer
{
public:
    enum class Type { Input, Output, GainReduction };

    LevelMeter (Type t, int channel, CraftLimitProcessor& proc);
    ~LevelMeter() override;

    void timerCallback() override;
    void paint (juce::Graphics&) override;

    void setLabel (const juce::String& l) { label_ = l; repaint(); }

private:
    Type                   type_;
    int                    channel_;
    CraftLimitProcessor&   processor_;
    float                  level_     { -144.0f };
    float                  peakLevel_ { -144.0f };
    float                  peakHold_  { -144.0f };
    int                    peakTimer_ { 0 };
    juce::String           label_;

    static constexpr float MIN_DB = -60.0f;
    static constexpr float MAX_DB =  6.0f;
    static constexpr int   PEAK_HOLD_FRAMES = 80;
};

// ─── Labelled Knob ───────────────────────────────────────────────────────────
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (const juce::String& paramID,
                  const juce::String& label,
                  CraftLimitProcessor& proc,
                  juce::Colour accent = juce::Colour (0xFFFF6B35));

    void resized() override;
    void paint   (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::Label  label_;
    juce::Label  valueLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;

    CraftLimitProcessor& processor_;
    juce::Colour         accent_;
};

// ─── Main Editor ─────────────────────────────────────────────────────────────
class CraftLimitEditor : public juce::AudioProcessorEditor
{
public:
    explicit CraftLimitEditor (CraftLimitProcessor&);
    ~CraftLimitEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    CraftLimitProcessor& processor_;
    CraftLookAndFeel     lnf_;

    // Knobs
    std::unique_ptr<LabelledKnob> inputGainKnob_;
    std::unique_ptr<LabelledKnob> thresholdKnob_;
    std::unique_ptr<LabelledKnob> ceilingKnob_;
    std::unique_ptr<LabelledKnob> lookaheadKnob_;
    std::unique_ptr<LabelledKnob> attackKnob_;
    std::unique_ptr<LabelledKnob> releaseKnob_;

    // Algorithm buttons
    juce::ComboBox algoBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algoAttachment_;

    // Toggles
    juce::ToggleButton truePeakButton_  { "True Peak" };
    juce::ComboBox     oversamplingBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  truePeakAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment_;

    // Meters
    std::unique_ptr<LevelMeter> meterInL_, meterInR_;
    std::unique_ptr<LevelMeter> meterOutL_, meterOutR_;
    std::unique_ptr<LevelMeter> meterGR_;

    // GR numeric display
    juce::Label grLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftLimitEditor)
};
