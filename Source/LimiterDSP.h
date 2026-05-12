#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

namespace CraftAudio
{

// ─── Algorithm Presets ───────────────────────────────────────────────────────
struct AlgoPreset
{
    float attackMul;   // multiplier on user attack time
    float releaseMul;  // multiplier on user release time
    float holdMs;      // hold time before release begins
    float knee;        // soft-knee width in dB (0 = hard knee)
};

static const AlgoPreset PRESETS[] =
{
    { 1.0f,  1.0f,  0.0f, 0.0f },  // Transparent
    { 0.5f,  2.0f,  2.0f, 0.5f },  // Dynamic
    { 0.1f,  0.3f,  0.0f, 0.0f },  // Aggressive
    { 0.05f, 0.8f,  5.0f, 1.0f },  // Surgical
    { 0.8f,  1.5f,  1.0f, 0.3f },  // Bus
};

static const char* PRESET_NAMES[] = {
    "Transparent", "Dynamic", "Aggressive", "Surgical", "Bus"
};
static constexpr int NUM_PRESETS = 5;

// Maximum lookahead time the buffer is sized for. Any user value below this
// is allowed at runtime without re-allocation.
static constexpr float MAX_LOOKAHEAD_MS = 30.0f;

// ─── Per-channel delay line + peak scanner ──────────────────────────────────
class LimiterChannel
{
public:
    LimiterChannel() = default;

    // Allocates the delay buffer once for `maxLookaheadSamples` (one-shot,
    // not real-time-safe). Subsequent lookahead changes go through
    // setCurrentLookahead() and do NOT reallocate.
    void prepare (double sampleRate, int maxBlockSize, int maxLookaheadSamples);
    void reset();

    // Real-time-safe: change how much of the pre-allocated buffer is in use.
    // Caller guarantees newLookahead <= maxLookaheadSamples passed to prepare().
    void setCurrentLookahead (int newLookahead);

    // Phase 1 — write a new input sample into the delay line at writePos.
    void writeSample (float input);

    // Phase 2 — scan the L most recent input samples (the upcoming output
    // window) and return the max absolute value found.
    float scanLookaheadPeak() const;

    // Phase 3 — read the sample currently due at the output (delayed by L).
    float readDelayedSample() const;

    // Phase 4 — advance the write position by one sample.
    void advance();

private:
    double sampleRate_ = 44100.0;
    int    bufSize_   = 0;
    int    lookahead_ = 0;
    int    writePos_  = 0;

    std::vector<float> delayBuffer_;
};

// ─── Parameters passed in from the host/processor ───────────────────────────
//
// truePeak and oversamplingFactor have moved out of this struct: they are
// processor-level concerns (handled by juce::dsp::Oversampling outside this
// DSP module).
//
struct LimiterParams
{
    float inputGainDb     = 0.0f;
    float thresholdDb     = 0.0f;
    float outputCeilingDb = -0.3f;
    float lookaheadMs     = 5.0f;
    float attackMs        = 0.5f;
    float releaseMs       = 150.0f;
    int   algoIndex       = 0;
};

// ─── Stereo Limiter (owns the linked envelope) ──────────────────────────────
class StereoLimiter
{
public:
    StereoLimiter() = default;

    // Allocates buffers for the MAXIMUM expected sample rate (typically
    // baseSampleRate × maxOversamplingFactor). At runtime the effective rate
    // can be changed via setEffectiveSampleRate without re-allocation.
    void prepare (double maxSampleRate, int maxBlockSize);
    void reset();

    // Update the effective sample rate (e.g. when the host's oversampling
    // factor changes). Re-derives all time-dependent coefficients without
    // touching the delay buffers. Real-time-safe.
    void setEffectiveSampleRate (double effectiveSampleRate);

    void setParams (const LimiterParams& p);

    // Process a stereo block in place at the current effective sample rate.
    void processBlock (float* left, float* right, int numSamples);

    // Metering (called from the GUI thread).
    float getGainReductionDb() const   { return gainReductionDb_; }
    float getInputPeakDb  (int ch) const { return inputPeakDb_[ch]; }
    float getOutputPeakDb (int ch) const { return outputPeakDb_[ch]; }

    int   getCurrentLookaheadSamples() const { return lookaheadSamples_; }

private:
    double   sampleRate_   = 44100.0;
    int      maxBlockSize_ = 512;

    LimiterParams  params_;
    LimiterChannel channels_[2];

    // Pre-computed coefficients (refreshed when params or sample rate change).
    float thresholdLinear_     = 1.0f;
    float outputCeilingLinear_ = 1.0f;
    float inputGainLinear_     = 1.0f;
    float attackCoeff_         = 0.0f;
    float releaseCoeff_        = 0.0f;
    int   holdSamples_         = 0;
    int   lookaheadSamples_    = 441;

    // Linked envelope state — shared across both channels.
    float envGain_     = 1.0f;
    int   holdCounter_ = 0;

    // Metering
    float inputPeakDb_[2]  = { -144.0f, -144.0f };
    float outputPeakDb_[2] = { -144.0f, -144.0f };
    float gainReductionDb_ = 0.0f;

    // ── Helpers ────────────────────────────────────────────────────────────
    static float makeCoeff (float ms, double sampleRate)
    {
        if (ms <= 0.0f) return 0.0f;
        return std::exp (-1.0f / static_cast<float> (ms * 0.001 * sampleRate));
    }

    static float dbToLinear (float db)  { return std::pow (10.0f, db / 20.0f); }
    static float linearToDb (float lin) { return lin > 0.0f ? 20.0f * std::log10 (lin) : -144.0f; }

    void  refreshCoefficients();   // recompute attack/release/hold/lookahead from params + sampleRate
    float computeTargetGain (float peak, float kneeDb) const;
    void  updateEnvelope (float targetGain);
};

} // namespace CraftAudio
