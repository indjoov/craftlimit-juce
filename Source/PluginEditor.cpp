#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace juce;

// ─────────────────────────────────────────────────────────────────────────────
// CraftLookAndFeel
// ─────────────────────────────────────────────────────────────────────────────

CraftLookAndFeel::CraftLookAndFeel()
{
    setColour (Slider::thumbColourId,           accentOrange);
    setColour (Slider::rotarySliderFillColourId, accentOrange);
    setColour (Slider::rotarySliderOutlineColourId, bgPanel);
    setColour (Slider::textBoxTextColourId,      Colour (0xFFAAAAAA));
    setColour (Slider::textBoxBackgroundColourId, bgPanel);
    setColour (Slider::textBoxOutlineColourId,   Colour (0xFF2A2A4A));
    setColour (Label::textColourId,              Colour (0xFF888888));
    setColour (ComboBox::backgroundColourId,     bgPanel);
    setColour (ComboBox::textColourId,           Colour (0xFFAAAAAA));
    setColour (ComboBox::outlineColourId,        Colour (0xFF2A2A4A));
    setColour (ComboBox::arrowColourId,          accentOrange);
    setColour (PopupMenu::backgroundColourId,    bgMid);
    setColour (PopupMenu::textColourId,          Colour (0xFFAAAAAA));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xFF2A2A4A));
    setColour (ToggleButton::textColourId,       Colour (0xFF888888));
    setColour (ToggleButton::tickColourId,       accentPurple);
    setColour (ToggleButton::tickDisabledColourId, Colour (0xFF333333));
}

void CraftLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          Slider& slider)
{
    const float cx  = x + width  * 0.5f;
    const float cy  = y + height * 0.5f;
    const float r   = jmin (width, height) * 0.5f - 6.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Background track
    Path track;
    track.addCentredArc (cx, cy, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (bgPanel.brighter (0.3f));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    // Filled arc
    if (sliderPos > 0.0f)
    {
        // Get accent colour from slider (stored in textBoxTextColour for simplicity)
        Colour accent = slider.findColour (Slider::rotarySliderFillColourId);

        Path fill;
        fill.addCentredArc (cx, cy, r, r, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (fill, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Knob body
    float kr = r - 8.0f;
    ColourGradient grad (Colour (0xFF4A4A6A), cx - kr * 0.3f, cy - kr * 0.3f,
                         Colour (0xFF1E1E2E), cx + kr, cy + kr, true);
    g.setGradientFill (grad);
    g.fillEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);

    g.setColour (Colour (0xFF2A2A4A));
    g.drawEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    // Pointer line
    const float px = cx + (kr - 4.0f) * std::sin (angle);
    const float py = cy - (kr - 4.0f) * std::cos (angle);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.drawLine (cx, cy, px, py, 2.0f);
}

void CraftLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float /*minPos*/, float /*maxPos*/,
                                          Slider::SliderStyle style, Slider& slider)
{
    // Simple horizontal bar for any linear sliders we might add later
    (void)style;
    g.setColour (bgPanel.brighter (0.3f));
    g.fillRoundedRectangle ((float)x, (float)y + height * 0.4f, (float)width, height * 0.2f, 2.0f);

    float pos = sliderPos - x;
    g.setColour (slider.findColour (Slider::rotarySliderFillColourId));
    g.fillRoundedRectangle ((float)x, (float)y + height * 0.4f, pos, height * 0.2f, 2.0f);
}

Font CraftLookAndFeel::getLabelFont (Label&)
{
    return Font ("Courier New", 10.0f, Font::plain);
}

// ─────────────────────────────────────────────────────────────────────────────
// LevelMeter
// ─────────────────────────────────────────────────────────────────────────────

LevelMeter::LevelMeter (Type t, int channel, CraftLimitProcessor& proc)
    : type_ (t), channel_ (channel), processor_ (proc)
{
    startTimerHz (30);  // 30 fps meter refresh
}

LevelMeter::~LevelMeter()
{
    stopTimer();
}

void LevelMeter::timerCallback()
{
    float newLevel;
    if (type_ == Type::GainReduction)
        newLevel = processor_.getGainReductionDb();
    else if (type_ == Type::Input)
        newLevel = processor_.getInputPeakDb (channel_);
    else
        newLevel = processor_.getOutputPeakDb (channel_);

    // Ballistics: fast attack, slow release
    if (newLevel > level_)
        level_ = newLevel;
    else
        level_ = jmax (level_ - 0.8f, newLevel);  // ~0.8 dB/frame release

    // Peak hold
    if (newLevel > peakHold_)
    {
        peakHold_  = newLevel;
        peakTimer_ = 0;
    }
    else
    {
        peakTimer_++;
        if (peakTimer_ > PEAK_HOLD_FRAMES)
            peakHold_ = jmax (peakHold_ - 0.3f, MIN_DB);
    }

    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight() - 16.0f;  // room for label at bottom
    const float top = 2.0f;

    // Background
    g.setColour (Colour (0xFF0A0A14));
    g.fillRoundedRectangle (0, top, w, h, 3.0f);

    // Segments (22 segments covering -60 to +6 dB)
    const int   numSegs = 22;
    const float segH    = h / numSegs;
    const float rangeDb = MAX_DB - MIN_DB;

    for (int i = 0; i < numSegs; ++i)
    {
        float segTop  = top + i * segH;
        float segDb   = MAX_DB - (i / (float)(numSegs - 1)) * rangeDb;
        float fillPct = (level_ - MIN_DB) / rangeDb;

        bool isActive;
        if (type_ == Type::GainReduction)
        {
            // GR meter: fills downward from 0 (level is negative)
            float grPct = jlimit (0.0f, 1.0f, -level_ / 30.0f);
            isActive = (i / (float)numSegs) < grPct;
        }
        else
        {
            float normVal = (level_ - MIN_DB) / rangeDb;
            isActive = normVal > (1.0f - (i + 1) / (float)numSegs);
        }

        Colour segColour;
        if (type_ == Type::GainReduction)
            segColour = isActive ? (level_ < -18.0f ? Colour (0xFFFF6B35) :
                                    level_ < -6.0f  ? Colour (0xFFF59E0B) :
                                                      Colour (0xFFEF4444))
                                 : Colour (0xFF1A1A2E);
        else
            segColour = isActive ? (segDb > -3.0f  ? Colour (0xFFEF4444) :
                                    segDb > -9.0f  ? Colour (0xFFF59E0B) :
                                    type_ == Type::Input ? Colour (0xFF22C55E)
                                                         : Colour (0xFF06B6D4))
                                 : Colour (0xFF1A1A2E);

        g.setColour (segColour);
        g.fillRoundedRectangle (1.0f, segTop + 0.5f, w - 2.0f, segH - 1.5f, 1.0f);
    }

    // Peak hold line
    if (type_ != Type::GainReduction && peakHold_ > MIN_DB)
    {
        float pkPct = (peakHold_ - MIN_DB) / rangeDb;
        float pkY   = top + h * (1.0f - pkPct);
        Colour pkCol = peakHold_ > -3.0f ? Colour (0xFFEF4444) :
                       peakHold_ > -9.0f ? Colour (0xFFF59E0B) :
                       type_ == Type::Input ? Colour (0xFF22C55E) : Colour (0xFF06B6D4);
        g.setColour (pkCol);
        g.fillRect (1.0f, pkY, w - 2.0f, 2.0f);
    }

    // Label
    g.setColour (Colour (0xFF555555));
    g.setFont (Font ("Courier New", 8.0f, Font::plain));
    g.drawText (label_, 0, (int)(top + h + 2), (int)w, 12, Justification::centred);
}

// ─────────────────────────────────────────────────────────────────────────────
// LabelledKnob
// ─────────────────────────────────────────────────────────────────────────────

LabelledKnob::LabelledKnob (const String& paramID, const String& labelText,
                             CraftLimitProcessor& proc, Colour accent)
    : processor_ (proc), accent_ (accent)
{
    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 60, 16);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);

    label_.setText (labelText, dontSendNotification);
    label_.setJustificationType (Justification::centred);
    label_.setFont (Font ("Courier New", 9.0f, Font::plain));
    label_.setColour (Label::textColourId, Colour (0xFF666666));
    addAndMakeVisible (label_);

    attachment_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, slider);
}

void LabelledKnob::resized()
{
    auto b = getLocalBounds();
    label_ .setBounds (b.removeFromBottom (14));
    slider .setBounds (b);
}

void LabelledKnob::paint (Graphics&) {}

// ─────────────────────────────────────────────────────────────────────────────
// CraftLimitEditor
// ─────────────────────────────────────────────────────────────────────────────

CraftLimitEditor::CraftLimitEditor (CraftLimitProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p)
{
    setLookAndFeel (&lnf_);
    setSize (740, 340);
    setResizable (false, false);

    // ── Knobs ─────────────────────────────────────────────────────────────
    inputGainKnob_  = std::make_unique<LabelledKnob> (ParamID::inputGain,     "INPUT GAIN",  p, Colour (0xFF7C3AED));
    thresholdKnob_  = std::make_unique<LabelledKnob> (ParamID::threshold,     "THRESHOLD",   p, Colour (0xFFFF6B35));
    ceilingKnob_    = std::make_unique<LabelledKnob> (ParamID::outputCeiling, "CEILING",     p, Colour (0xFFF59E0B));
    lookaheadKnob_  = std::make_unique<LabelledKnob> (ParamID::lookahead,     "LOOKAHEAD",   p, Colour (0xFF06B6D4));
    attackKnob_     = std::make_unique<LabelledKnob> (ParamID::attack,        "ATTACK",      p, Colour (0xFF22C55E));
    releaseKnob_    = std::make_unique<LabelledKnob> (ParamID::release,       "RELEASE",     p, Colour (0xFF84CC16));

    for (auto* k : { inputGainKnob_.get(), thresholdKnob_.get(), ceilingKnob_.get(),
                     lookaheadKnob_.get(), attackKnob_.get(), releaseKnob_.get() })
        addAndMakeVisible (k);

    // ── Algorithm ComboBox ────────────────────────────────────────────────
    algoBox_.addItem ("Transparent", 1);
    algoBox_.addItem ("Dynamic",     2);
    algoBox_.addItem ("Aggressive",  3);
    algoBox_.addItem ("Surgical",    4);
    algoBox_.addItem ("Bus",         5);
    addAndMakeVisible (algoBox_);
    algoAttachment_ = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.apvts, ParamID::algo, algoBox_);

    // ── True Peak toggle ──────────────────────────────────────────────────
    addAndMakeVisible (truePeakButton_);
    truePeakAttachment_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, ParamID::truePeak, truePeakButton_);

    // ── Oversampling ComboBox ─────────────────────────────────────────────
    oversamplingBox_.addItem ("1x", 1);
    oversamplingBox_.addItem ("2x", 2);
    oversamplingBox_.addItem ("4x", 3);
    addAndMakeVisible (oversamplingBox_);
    oversamplingAttachment_ = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.apvts, ParamID::oversampling, oversamplingBox_);

    // ── Meters ────────────────────────────────────────────────────────────
    meterInL_  = std::make_unique<LevelMeter> (LevelMeter::Type::Input,         0, p);
    meterInR_  = std::make_unique<LevelMeter> (LevelMeter::Type::Input,         1, p);
    meterGR_   = std::make_unique<LevelMeter> (LevelMeter::Type::GainReduction, 0, p);
    meterOutL_ = std::make_unique<LevelMeter> (LevelMeter::Type::Output,        0, p);
    meterOutR_ = std::make_unique<LevelMeter> (LevelMeter::Type::Output,        1, p);

    meterInL_ ->setLabel ("L");
    meterInR_ ->setLabel ("R");
    meterGR_  ->setLabel ("GR");
    meterOutL_->setLabel ("L");
    meterOutR_->setLabel ("R");

    for (auto* m : { meterInL_.get(), meterInR_.get(), meterGR_.get(),
                     meterOutL_.get(), meterOutR_.get() })
        addAndMakeVisible (m);

    // ── GR Label ─────────────────────────────────────────────────────────
    grLabel_.setFont (Font ("Courier New", 11.0f, Font::plain));
    grLabel_.setColour (Label::textColourId, Colour (0xFFFF6B35));
    grLabel_.setJustificationType (Justification::centred);
    addAndMakeVisible (grLabel_);
}

CraftLimitEditor::~CraftLimitEditor()
{
    setLookAndFeel (nullptr);
}

void CraftLimitEditor::paint (Graphics& g)
{
    // Background
    g.fillAll (Colour (0xFF0A0A14));

    // Header band
    g.setColour (Colour (0xFF1A0A28));
    g.fillRect (0, 0, getWidth(), 40);

    // Header border
    g.setColour (Colour (0xFF2A2A4A));
    g.drawLine (0.0f, 40.0f, (float)getWidth(), 40.0f, 1.0f);

    // Brand name
    g.setFont (Font ("Arial Black", 20.0f, Font::bold));
    g.setColour (Colour (0xFFFF6B35));
    g.drawText ("CRAFTLIMIT", 20, 8, 200, 24, Justification::centredLeft);

    // Subtitle
    g.setFont (Font ("Courier New", 9.0f, Font::plain));
    g.setColour (Colour (0xFF444466));
    g.drawText ("TRUE PEAK LIMITER  //  CRAFT AUDIO TOOLS", 20, 26, 400, 12, Justification::centredLeft);

    // Version
    g.setFont (Font ("Courier New", 8.0f, Font::plain));
    g.setColour (Colour (0xFF333355));
    g.drawText ("v1.0.0", getWidth() - 60, 15, 50, 14, Justification::centredRight);

    // Section labels
    g.setFont (Font ("Courier New", 8.0f, Font::plain));
    g.setColour (Colour (0xFF444466));
    g.drawText ("ALGORITHM", 20, 48, 100, 10, Justification::centredLeft);
    g.drawText ("GAIN / DYNAMICS", 20, 90, 200, 10, Justification::centredLeft);
    g.drawText ("TIMING", 260, 90, 100, 10, Justification::centredLeft);
    g.drawText ("INPUT", 520, 48, 60, 10, Justification::centred);
    g.drawText ("OUTPUT", 640, 48, 70, 10, Justification::centred);

    // Separator lines
    g.setColour (Colour (0xFF1A1A2E));
    g.drawLine (500.0f, 44.0f, 500.0f, (float)getHeight() - 10.0f, 1.0f);
    g.drawLine (248.0f, 88.0f, 248.0f, (float)getHeight() - 10.0f, 1.0f);

    // Meter section bg
    g.setColour (Colour (0xFF0D0D18));
    g.fillRoundedRectangle (508.0f, 42.0f, 224.0f, (float)getHeight() - 52.0f, 6.0f);
}

void CraftLimitEditor::resized()
{
    // ── Algorithm row ─────────────────────────────────────────────────────
    algoBox_      .setBounds (20, 58, 140, 24);
    truePeakButton_.setBounds (170, 58, 80, 24);
    oversamplingBox_.setBounds (258, 58, 50, 24);

    // ── Knob row ──────────────────────────────────────────────────────────
    const int knobSize = 80;
    const int knobTop  = 95;

    inputGainKnob_->setBounds (20,  knobTop, knobSize, knobSize + 28);
    thresholdKnob_->setBounds (108, knobTop, knobSize, knobSize + 28);
    ceilingKnob_  ->setBounds (196, knobTop, knobSize, knobSize + 28);

    lookaheadKnob_->setBounds (262, knobTop, knobSize, knobSize + 28);
    attackKnob_   ->setBounds (350, knobTop, knobSize, knobSize + 28);
    releaseKnob_  ->setBounds (438, knobTop, knobSize, knobSize + 28);

    // ── Meters ────────────────────────────────────────────────────────────
    const int meterTop = 60;
    const int meterH   = getHeight() - meterTop - 10;
    const int meterW   = 18;

    meterInL_ ->setBounds (520, meterTop, meterW, meterH);
    meterInR_ ->setBounds (540, meterTop, meterW, meterH);
    meterGR_  ->setBounds (575, meterTop, meterW, meterH);
    meterOutL_->setBounds (615, meterTop, meterW, meterH);
    meterOutR_->setBounds (635, meterTop, meterW, meterH);

    // GR numeric display
    grLabel_.setBounds (560, getHeight() - 30, 80, 20);
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────

AudioProcessorEditor* CraftLimitProcessor::createEditor()
{
    return new CraftLimitEditor (*this);
}
