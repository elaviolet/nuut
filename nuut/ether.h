#pragma once

#include "daisysp.h"

#include <cmath>
#include <cstddef>

// ============================================================
// NUUT ETHEREAL STAGE
//
// Global stage driven by the Alchemy knob. It replaces the
// per-voice comb + filter, so it costs one comb instead of
// three and only hears what is actually being played.
//
//   ProcessColor : slow-sweeping low-pass blended with a comb
//                  filter whose delay is modulated by a very
//                  slow LFO (fractional delay, no zipper noise).
//   ProcessEcho  : two-tap stereo echo with darkened feedback
//                  and a slow wobble, added to the stereo bus.
//
// All tuning constants are in the namespace below.
// ============================================================

namespace nuut_ethereal
{
// Comb filter
constexpr float kCombCenterHz = 600.0f;
constexpr float kCombDepth    = 0.12f;  // +-12% of the delay time
constexpr float kCombLfoHz    = 0.09f;
constexpr float kCombFeedback = 0.80f;
constexpr float kCombDamp     = 0.45f;  // one-pole low-pass in the loop
constexpr float kCombMaxMix   = 0.45f;  // comb amount at Alchemy = 1

// Sweeping low-pass
constexpr float kFilterCenterHz = 2400.0f;
constexpr float kFilterSweepHz  = 900.0f;
constexpr float kFilterLfoHz    = 0.057f;
constexpr float kFilterRes      = 0.45f;
constexpr float kFilterMaxMix   = 0.55f;

// Stereo echo
constexpr float kEchoTimeL            = 0.31f;  // seconds
constexpr float kEchoTimeR            = 0.47f;
constexpr float kEchoModSeconds       = 0.006f;
constexpr float kEchoLfoHz            = 0.11f;
constexpr float kEchoDamp             = 0.30f;
constexpr float kEchoBaseSend         = 0.10f;  // always a little present
constexpr float kEchoAlchemySend      = 0.45f;  // added at Alchemy = 1
constexpr float kEchoBaseFeedback     = 0.42f;
constexpr float kEchoAlchemyFeedback  = 0.18f;

constexpr std::size_t kCombLineSize = 2048;
constexpr std::size_t kEchoLineSize = 24000;
} 


inline float EtherealSin01(float phase)
{
    const float x = 2.0f * phase - 1.0f;
    float y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}

class EtherealStage
{
public:
    void Init(float sampleRate)
    {
        using namespace nuut_ethereal;

        sampleRate_    = sampleRate;
        invSampleRate_ = 1.0f / sampleRate;

        combLine_.Init();
        echoLine_.Init();

        filter_.Init(sampleRate);
        filter_.SetFreq(kFilterCenterHz);
        filter_.SetRes(kFilterRes);

        combBaseDelay_  = sampleRate / kCombCenterHz;
        echoBaseL_      = kEchoTimeL * sampleRate;
        echoBaseR_      = kEchoTimeR * sampleRate;
        echoModSamples_ = kEchoModSeconds * sampleRate;

        // Keep every read position inside the delay lines.
        echoMaxDelay_ = static_cast<float>(kEchoLineSize - 2);

        combPhase_   = 0.0f;
        filterPhase_ = 0.25f;
        echoPhase_   = 0.60f;

        combDamp_ = 0.0f;
        echoDamp_ = 0.0f;
        counter_  = 0;

        SetAlchemy(0.0f);
    }

    // Call once per audio block with the Alchemy knob (0..1).
    void SetAlchemy(float alchemy)
    {
        using namespace nuut_ethereal;

        const float a = fmaxf(0.0f, fminf(1.0f, alchemy));

        combMix_      = a * kCombMaxMix;
        filterMix_    = a * kFilterMaxMix;
        echoSend_     = kEchoBaseSend + a * kEchoAlchemySend;
        echoFeedback_ = kEchoBaseFeedback + a * kEchoAlchemyFeedback;
    }

    // Mono in, mono out: low-pass + modulated comb.
    float ProcessColor(float in)
    {
        using namespace nuut_ethereal;

        combPhase_ += kCombLfoHz * invSampleRate_;
        if(combPhase_ >= 1.0f)
            combPhase_ -= 1.0f;

        filterPhase_ += kFilterLfoHz * invSampleRate_;
        if(filterPhase_ >= 1.0f)
            filterPhase_ -= 1.0f;

        // Filter cutoff is updated every 32 samples (SetFreq is not free).
        if((++counter_ & 31u) == 0u)
        {
            filter_.SetFreq(kFilterCenterHz
                            + kFilterSweepHz * EtherealSin01(filterPhase_));
        }

        filter_.Process(in);
        const float low = filter_.Low();
        const float x   = in + (low - in) * filterMix_;

        // Comb with a slowly modulated, fractionally interpolated delay.
        const float lfo   = EtherealSin01(combPhase_);
        const float delay = combBaseDelay_ * (1.0f - kCombDepth * lfo);

        const float d = combLine_.Read(delay);
        combDamp_ += kCombDamp * (d - combDamp_);
        combLine_.Write(x * 0.5f + combDamp_ * kCombFeedback);

        return x + (d - x) * combMix_;
    }

    // Mono in, stereo echo out (already scaled by the send amount).
    void ProcessEcho(float in, float& left, float& right)
    {
        using namespace nuut_ethereal;

        echoPhase_ += kEchoLfoHz * invSampleRate_;
        if(echoPhase_ >= 1.0f)
            echoPhase_ -= 1.0f;

        const float mod = EtherealSin01(echoPhase_) * echoModSamples_;

        float delayL = echoBaseL_ + mod;
        float delayR = echoBaseR_ - mod;
        if(delayL > echoMaxDelay_)
            delayL = echoMaxDelay_;
        if(delayR > echoMaxDelay_)
            delayR = echoMaxDelay_;

        const float tapL = echoLine_.Read(delayL);
        const float tapR = echoLine_.Read(delayR);

        // Each repeat gets a little darker.
        echoDamp_ += kEchoDamp * ((tapL + tapR) * 0.5f - echoDamp_);
        echoLine_.Write(in + echoDamp_ * echoFeedback_);

        left  = tapL * echoSend_;
        right = tapR * echoSend_;
    }

private:
    daisysp::DelayLine<float, nuut_ethereal::kCombLineSize> combLine_;
    daisysp::DelayLine<float, nuut_ethereal::kEchoLineSize> echoLine_;
    daisysp::Svf filter_;

    float sampleRate_    = 48000.0f;
    float invSampleRate_ = 1.0f / 48000.0f;

    float combBaseDelay_  = 80.0f;
    float echoBaseL_      = 14880.0f;
    float echoBaseR_      = 22560.0f;
    float echoModSamples_ = 288.0f;
    float echoMaxDelay_   = 23998.0f;

    float combPhase_   = 0.0f;
    float filterPhase_ = 0.25f;
    float echoPhase_   = 0.60f;

    float combDamp_ = 0.0f;
    float echoDamp_ = 0.0f;

    float combMix_      = 0.0f;
    float filterMix_    = 0.0f;
    float echoSend_     = 0.10f;
    float echoFeedback_ = 0.42f;

    unsigned counter_ = 0;
};