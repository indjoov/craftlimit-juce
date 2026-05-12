#include "LimiterDSP.h"

namespace CraftAudio
{

// ─────────────────────────────────────────────────────────────────────────────
// LimiterChannel
// ─────────────────────────────────────────────────────────────────────────────

void LimiterChannel::prepare (double sampleRate, int maxBlockSize, int maxLookaheadSamples)
{
    sampleRate_ = sampleRate;
    lookahead_  = maxLookaheadSamples;          // initial value, can be reduced at runtime
    bufSize_    = maxLookaheadSamples + maxBlockSize + 64;
    delayBuffer_.assign (bufSize_, 0.0f);
    writePos_   = 0;
}

void LimiterChannel::reset()
{
    std::fill (delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writePos_ = 0;
}

void LimiterChannel::setCurrentLookahead (int newLookahead)
{
    // Clamp into the buffer's safe range. The pre-allocated buffer was sized
    // for the maximum lookahead, so any value at or below that is OK.
    if (newLookahead < 0)                     newLookahead = 0;
    if (newLookahead >= bufSize_)             newLookahead = bufSize_ - 1;
    lookahead_ = newLookahead;
}

void LimiterChannel::writeSample (float input)
{
    delayBuffer_[writePos_] = input;
}

float LimiterChannel::scanLookaheadPeak() const
{
    // Scan the L most recent samples (writePos backwards). These are the
    // samples that will be output over the next L iterations.
    float peak = 0.0f;
    for (int j = 0; j < lookahead_; ++j)
    {
        int idx = (writePos_ - j + bufSize_) % bufSize_;
        float s = std::abs (delayBuffer_[idx]);
        if (s > peak) peak = s;
    }
    return peak;
}

float LimiterChannel::readDelayedSample() const
{
    int readPos = (writePos_ - lookahead_ + bufSize_) % bufSize_;
    return delayBuffer_[readPos];
}

void LimiterChannel::advance()
{
    writePos_ = (writePos_ + 1) % bufSize_;
}

// ─────────────────────────────────────────────────────────────────────────────
// StereoLimiter
// ─────────────────────────────────────────────────────────────────────────────

void StereoLimiter::prepare (double maxSampleRate, int maxBlockSize)
{
    sampleRate_   = maxSampleRate;
    maxBlockSize_ = maxBlockSize;

    // Size each channel's delay buffer for the maximum lookahead time at the
    // maximum sample rate. Any smaller lookahead at runtime fits.
    const int maxLookaheadSamples =
        static_cast<int> (MAX_LOOKAHEAD_MS * 0.001 * maxSampleRate);

    for (auto& ch : channels_)
        ch.prepare (maxSampleRate, maxBlockSize, maxLookaheadSamples);

    envGain_     = 1.0f;
    holdCounter_ = 0;

    refreshCoefficients();
}

void StereoLimiter::reset()
{
    for (auto& ch : channels_) ch.reset();
    inputPeakDb_[0]  = inputPeakDb_[1]  = -144.0f;
    outputPeakDb_[0] = outputPeakDb_[1] = -144.0f;
    gainReductionDb_ = 0.0f;
    envGain_         = 1.0f;
    holdCounter_     = 0;
}

void StereoLimiter::setEffectiveSampleRate (double effectiveSampleRate)
{
    sampleRate_ = effectiveSampleRate;
    refreshCoefficients();
}

void StereoLimiter::setParams (const LimiterParams& p)
{
    params_ = p;
    refreshCoefficients();
}

void StereoLimiter::refreshCoefficients()
{
    const AlgoPreset& preset = PRESETS[std::min (params_.algoIndex, NUM_PRESETS - 1)];

    thresholdLinear_     = dbToLinear (params_.thresholdDb);
    outputCeilingLinear_ = dbToLinear (params_.outputCeilingDb);
    inputGainLinear_     = dbToLinear (params_.inputGainDb);

    attackCoeff_  = makeCoeff (params_.attackMs  * preset.attackMul,  sampleRate_);
    releaseCoeff_ = makeCoeff (params_.releaseMs * preset.releaseMul, sampleRate_);
    holdSamples_  = static_cast<int> (preset.holdMs * 0.001 * sampleRate_);

    lookaheadSamples_ = static_cast<int> (params_.lookaheadMs * 0.001 * sampleRate_);
    for (auto& ch : channels_)
        ch.setCurrentLookahead (lookaheadSamples_);
}

float StereoLimiter::computeTargetGain (float peak, float kneeDb) const
{
    if (peak <= 0.0f) return 1.0f;

    if (kneeDb > 0.0f)
    {
        const float peakDb   = linearToDb (peak);
        const float threshDb = linearToDb (thresholdLinear_);
        const float kHalf    = kneeDb * 0.5f;

        if (peakDb < threshDb - kHalf) return 1.0f;
        if (peakDb > threshDb + kHalf) return thresholdLinear_ / peak;

        const float x      = (peakDb - (threshDb - kHalf)) / kneeDb;
        const float gainDb = (threshDb - kHalf) + x * x * kHalf;
        return dbToLinear (gainDb) / peak;
    }

    return (peak > thresholdLinear_) ? thresholdLinear_ / peak : 1.0f;
}

void StereoLimiter::updateEnvelope (float targetGain)
{
    if (targetGain < envGain_)
    {
        holdCounter_ = holdSamples_;
        envGain_ = attackCoeff_ * envGain_ + (1.0f - attackCoeff_) * targetGain;
    }
    else if (holdCounter_ > 0)
    {
        holdCounter_--;
    }
    else
    {
        envGain_ = releaseCoeff_ * envGain_ + (1.0f - releaseCoeff_) * targetGain;
    }
    envGain_ = std::min (envGain_, 1.0f);
    envGain_ = std::max (envGain_, 0.0f);
}

void StereoLimiter::processBlock (float* left, float* right, int numSamples)
{
    float grMin      = 1.0f;
    float peakIn[2]  = { 0.0f, 0.0f };
    float peakOut[2] = { 0.0f, 0.0f };

    const float kneeDb = PRESETS[std::min (params_.algoIndex, NUM_PRESETS - 1)].knee;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i]  * inputGainLinear_;
        const float inR = right[i] * inputGainLinear_;

        if (std::abs (inL) > peakIn[0]) peakIn[0] = std::abs (inL);
        if (std::abs (inR) > peakIn[1]) peakIn[1] = std::abs (inR);

        channels_[0].writeSample (inL);
        channels_[1].writeSample (inR);

        // Stereo-linked peak detection.
        const float peakL = channels_[0].scanLookaheadPeak();
        const float peakR = channels_[1].scanLookaheadPeak();
        const float peak  = std::max (peakL, peakR);

        const float targetGain = computeTargetGain (peak, kneeDb);
        updateEnvelope (targetGain);

        float outL = channels_[0].readDelayedSample() * envGain_;
        float outR = channels_[1].readDelayedSample() * envGain_;

        // Hard ceiling clamp (catches small attack-ramp overshoots).
        if      (outL >  outputCeilingLinear_) outL =  outputCeilingLinear_;
        else if (outL < -outputCeilingLinear_) outL = -outputCeilingLinear_;
        if      (outR >  outputCeilingLinear_) outR =  outputCeilingLinear_;
        else if (outR < -outputCeilingLinear_) outR = -outputCeilingLinear_;

        channels_[0].advance();
        channels_[1].advance();

        left[i]  = outL;
        right[i] = outR;

        if (envGain_ < grMin) grMin = envGain_;
        if (std::abs (outL) > peakOut[0]) peakOut[0] = std::abs (outL);
        if (std::abs (outR) > peakOut[1]) peakOut[1] = std::abs (outR);
    }

    constexpr float meterRelease = 0.999f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float newPeakIn  = linearToDb (peakIn[ch]);
        const float newPeakOut = linearToDb (peakOut[ch]);
        inputPeakDb_[ch]  = std::max (newPeakIn,  meterRelease * inputPeakDb_[ch]);
        outputPeakDb_[ch] = std::max (newPeakOut, meterRelease * outputPeakDb_[ch]);
    }

    gainReductionDb_ = linearToDb (grMin);
}

} // namespace CraftAudio
