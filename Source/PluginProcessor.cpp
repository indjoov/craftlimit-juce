#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Parameter layout
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
CraftLimitProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::inputGain, "Input Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::threshold, "Threshold",
        juce::NormalisableRange<float> (-30.0f, 0.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::outputCeiling, "Output Ceiling",
        juce::NormalisableRange<float> (-6.0f, 0.0f, 0.01f), -0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::lookahead, "Lookahead",
        juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f, 0.5f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::attack, "Attack",
        juce::NormalisableRange<float> (0.1f, 50.0f, 0.01f, 0.3f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamID::release, "Release",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.3f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamID::algo, "Algorithm",
        juce::StringArray { "Transparent", "Dynamic", "Aggressive", "Surgical", "Bus" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamID::truePeak, "True Peak", true));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamID::oversampling, "Oversampling",
        juce::StringArray { "1x", "2x", "4x" }, 1));

    return { params.begin(), params.end() };
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

CraftLimitProcessor::CraftLimitProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CraftLimitState", createParameterLayout())
{
    for (auto& id : { ParamID::inputGain, ParamID::threshold, ParamID::outputCeiling,
                      ParamID::lookahead,  ParamID::attack,    ParamID::release,
                      ParamID::algo,       ParamID::truePeak,  ParamID::oversampling })
    {
        apvts.addParameterListener (id, this);
    }
}

CraftLimitProcessor::~CraftLimitProcessor()
{
    for (auto& id : { ParamID::inputGain, ParamID::threshold, ParamID::outputCeiling,
                      ParamID::lookahead,  ParamID::attack,    ParamID::release,
                      ParamID::algo,       ParamID::truePeak,  ParamID::oversampling })
    {
        apvts.removeParameterListener (id, this);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare / release
// ─────────────────────────────────────────────────────────────────────────────

void CraftLimitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate_ = sampleRate;
    baseBlockSize_  = samplesPerBlock;

    // Pre-allocate the 2× and 4× oversamplers; 1× is bypass (nullptr).
    using OS = juce::dsp::Oversampling<float>;

    oversamplers_[0].reset();
    oversamplers_[1] = std::make_unique<OS> (
        2, 1, OS::filterHalfBandFIREquiripple, true, true);
    oversamplers_[2] = std::make_unique<OS> (
        2, 2, OS::filterHalfBandFIREquiripple, true, true);

    oversamplers_[1]->initProcessing (static_cast<size_t> (samplesPerBlock));
    oversamplers_[2]->initProcessing (static_cast<size_t> (samplesPerBlock));

    // Limiter sized for the worst-case effective sample rate (4× base).
    constexpr int MAX_OS = 4;
    limiter_.prepare (sampleRate * MAX_OS, samplesPerBlock * MAX_OS);

    // Force a refresh on the first processBlock.
    paramsDirty_.store (true, std::memory_order_release);

    updateActiveOSIndex();
    updateReportedLatency();
}

void CraftLimitProcessor::releaseResources()
{
    limiter_.reset();
    if (oversamplers_[1]) oversamplers_[1]->reset();
    if (oversamplers_[2]) oversamplers_[2]->reset();
}

bool CraftLimitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter change listener (message thread, or audio thread for automation)
// ─────────────────────────────────────────────────────────────────────────────

void CraftLimitProcessor::parameterChanged (const juce::String& paramID, float /*newValue*/)
{
    // Signal that the limiter needs a refresh. The actual mutation happens
    // on the audio thread in processBlock (see refreshLimiterFromAPVTS).
    //
    // Using release ordering pairs with the acquire in processBlock so that
    // any prior APVTS stores are visible to the audio thread when it observes
    // the flag.
    paramsDirty_.store (true, std::memory_order_release);

    // Oversampler index is read on the audio thread to decide which OS
    // pipeline to run. Update it atomically here.
    if (paramID == ParamID::truePeak || paramID == ParamID::oversampling)
        updateActiveOSIndex();

    // Latency reporting goes through the host API and is safe to call from
    // any thread per JUCE. Always recompute since lookahead and OS factor
    // both affect total latency.
    updateReportedLatency();
}

// ─────────────────────────────────────────────────────────────────────────────
// Message-thread helpers
// ─────────────────────────────────────────────────────────────────────────────

int CraftLimitProcessor::effectiveOSFactor() const
{
    const bool truePeakOn =
        apvts.getRawParameterValue (ParamID::truePeak)->load() > 0.5f;
    if (! truePeakOn) return 1;

    const int osIdx =
        static_cast<int> (apvts.getRawParameterValue (ParamID::oversampling)->load());
    switch (osIdx) { case 0: return 1; case 1: return 2; case 2: return 4; }
    return 1;
}

void CraftLimitProcessor::updateActiveOSIndex()
{
    const int factor = effectiveOSFactor();
    int newIdx;
    switch (factor) { case 1: newIdx = 0; break;
                      case 2: newIdx = 1; break;
                      case 4: newIdx = 2; break;
                      default: newIdx = 0; }
    activeOSIndex_.store (newIdx, std::memory_order_release);
}

void CraftLimitProcessor::updateReportedLatency()
{
    const int factor = effectiveOSFactor();
    int idx;
    switch (factor) { case 1: idx = 0; break;
                      case 2: idx = 1; break;
                      case 4: idx = 2; break;
                      default: idx = 0; }

    int osLatencyBaseSamples = 0;
    if (idx > 0 && oversamplers_[idx] != nullptr)
        osLatencyBaseSamples = static_cast<int> (oversamplers_[idx]->getLatencyInSamples());

    const float lookaheadMs =
        apvts.getRawParameterValue (ParamID::lookahead)->load();
    const int lookaheadSamples =
        static_cast<int> (lookaheadMs * 0.001 * baseSampleRate_);

    setLatencySamples (osLatencyBaseSamples + lookaheadSamples);
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio-thread parameter refresh (called from processBlock)
// ─────────────────────────────────────────────────────────────────────────────

void CraftLimitProcessor::refreshLimiterFromAPVTS()
{
    // First, tell the limiter what effective sample rate to use. APVTS atomics
    // give tear-free reads here.
    const int factor = effectiveOSFactor();
    limiter_.setEffectiveSampleRate (baseSampleRate_ * factor);

    // Then push the user-facing parameters.
    CraftAudio::LimiterParams p;
    p.inputGainDb     = apvts.getRawParameterValue (ParamID::inputGain)->load();
    p.thresholdDb     = apvts.getRawParameterValue (ParamID::threshold)->load();
    p.outputCeilingDb = apvts.getRawParameterValue (ParamID::outputCeiling)->load();
    p.lookaheadMs     = apvts.getRawParameterValue (ParamID::lookahead)->load();
    p.attackMs        = apvts.getRawParameterValue (ParamID::attack)->load();
    p.releaseMs       = apvts.getRawParameterValue (ParamID::release)->load();
    p.algoIndex       = static_cast<int> (
                        apvts.getRawParameterValue (ParamID::algo)->load());

    limiter_.setParams (p);
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock — refresh-if-dirty, then oversample → limit → downsample
// ─────────────────────────────────────────────────────────────────────────────

void CraftLimitProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    // ── Cross-thread parameter handoff ───────────────────────────────────────
    // exchange(false) atomically reads the current value AND clears the flag.
    // If true, we have new parameters to absorb. acquire ordering pairs with
    // the release in parameterChanged.
    if (paramsDirty_.exchange (false, std::memory_order_acquire))
        refreshLimiterFromAPVTS();

    // ── Audio processing ─────────────────────────────────────────────────────
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float* left  = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1)
                                   : buffer.getWritePointer (0);

    const int osIdx = activeOSIndex_.load (std::memory_order_acquire);

    if (osIdx == 0 || oversamplers_[osIdx] == nullptr)
    {
        limiter_.processBlock (left, right, numSamples);
    }
    else
    {
        float* channelPointers[2] = { left, right };
        juce::dsp::AudioBlock<float> baseBlock (channelPointers, 2, (size_t) numSamples);

        auto upBlock = oversamplers_[osIdx]->processSamplesUp (baseBlock);

        const int upSamples = static_cast<int> (upBlock.getNumSamples());
        float* upL = upBlock.getChannelPointer (0);
        float* upR = upBlock.getChannelPointer (1);

        limiter_.processBlock (upL, upR, upSamples);

        oversamplers_[osIdx]->processSamplesDown (baseBlock);
    }

    // ── Publish meters (atomic, lock-free) ──────────────────────────────────
    gainReductionDb_.store (limiter_.getGainReductionDb());
    for (int ch = 0; ch < 2; ++ch)
    {
        inputPeakDb_[ch].store  (limiter_.getInputPeakDb (ch));
        outputPeakDb_[ch].store (limiter_.getOutputPeakDb (ch));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

double CraftLimitProcessor::getTailLengthSeconds() const
{
    return apvts.getRawParameterValue (ParamID::lookahead)->load() * 0.001;
}

void CraftLimitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void CraftLimitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CraftLimitProcessor();
}
