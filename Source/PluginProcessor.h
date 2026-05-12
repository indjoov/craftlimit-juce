#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "LimiterDSP.h"

// ─── Parameter IDs ───────────────────────────────────────────────────────────
namespace ParamID
{
    static const juce::String inputGain     = "inputGain";
    static const juce::String threshold     = "threshold";
    static const juce::String outputCeiling = "outputCeiling";
    static const juce::String lookahead     = "lookahead";
    static const juce::String attack        = "attack";
    static const juce::String release       = "release";
    static const juce::String algo          = "algo";
    static const juce::String truePeak      = "truePeak";
    static const juce::String oversampling  = "oversampling";
}

// ─────────────────────────────────────────────────────────────────────────────
class CraftLimitProcessor : public juce::AudioProcessor,
                             public juce::AudioProcessorValueTreeState::Listener
{
public:
    CraftLimitProcessor();
    ~CraftLimitProcessor() override;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CraftLimit"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Parameter change listener (called on message thread or by host) ─────
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // ── Metering (atomic → GUI-safe) ────────────────────────────────────────
    float getGainReductionDb()     const { return gainReductionDb_.load(); }
    float getInputPeakDb  (int ch) const { return inputPeakDb_[ch].load(); }
    float getOutputPeakDb (int ch) const { return outputPeakDb_[ch].load(); }

    // Public so the editor can attach sliders.
    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Audio-thread-only helpers ────────────────────────────────────────────
    // Re-reads all parameters from APVTS atomics and pushes them into the
    // limiter. Called only from processBlock (the audio thread), so the
    // limiter never sees concurrent mutation.
    void refreshLimiterFromAPVTS();

    // ── Message-thread helpers ───────────────────────────────────────────────
    void updateActiveOSIndex();       // atomic store of active oversampler
    void updateReportedLatency();     // calls setLatencySamples (host API)
    int  effectiveOSFactor() const;   // reads APVTS, returns 1/2/4

    CraftAudio::StereoLimiter limiter_;

    // Three pre-allocated oversamplers: 0 → 1× (nullptr/passthrough),
    // 1 → 2×, 2 → 4×. All allocated in prepareToPlay so the audio thread
    // can switch via a single atomic store without any allocation.
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers_;
    std::atomic<int> activeOSIndex_ { 0 };

    // ── Cross-thread coordination ───────────────────────────────────────────
    // Set on the message thread when any parameter changes; checked on the
    // audio thread at the start of processBlock. Using exchange() ensures
    // we never miss an update: if the flag is true, refresh; then atomically
    // clear it.
    //
    // This pattern means the limiter is mutated only from the audio thread,
    // so its internal members do not need to be atomic. The message thread
    // only writes one bool and (for OS changes) one int — both atomic and
    // both lock-free on every platform JUCE supports.
    std::atomic<bool> paramsDirty_ { true };

    double baseSampleRate_ = 44100.0;
    int    baseBlockSize_  = 512;

    // Meters: audio thread writes, GUI thread reads. Atomic loads/stores
    // are wait-free for fundamental types on all supported platforms.
    std::atomic<float> gainReductionDb_ { 0.0f };
    std::atomic<float> inputPeakDb_[2]  { { -144.0f }, { -144.0f } };
    std::atomic<float> outputPeakDb_[2] { { -144.0f }, { -144.0f } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftLimitProcessor)
};
