#pragma once

#include "daisy_seed.h"
#include "daisysp.h"

#include <cstddef>

struct CrystalStereo
{
    float left;
    float right;
};

// Three stereo effects selected by the A switch:
//   POS_CENTER : Ametista    - octave shimmer, wide and spatial
//   POS_UP     : Acquamarina - nautical ping-pong delay
//   POS_DOWN   : Ossidiana   - dark, resonant, slowly sweeping
//
// Mono in, stereo out. The output includes the input signal;
// the caller blends it with the dry signal using the wet knob.
class Crystals
{
public:
    void Init(float sampleRate);

    CrystalStereo Process(
        float input,
        int mode,
        float refraction,
        float radiance,
        bool active
    );

private:
    static constexpr std::size_t kLineSize = 28000; // about 0.58 s at 48 kHz

    // Stereo delay pair shared by the three modes
    daisysp::DelayLine<float, kLineSize> lineL;
    daisysp::DelayLine<float, kLineSize> lineR;

    // Ametista: octave shimmer inside the feedback loop
    daisysp::PitchShifter shifter;

    // Ossidiana: dark resonant filter
    daisysp::Svf darkFilter;

    // Acquamarina: filtered water noise
    daisysp::Svf waterFilter;
    daisysp::WhiteNoise noise;

    float sampleRate = 48000.0f;
    float invSampleRate = 1.0f / 48000.0f;

    // Smoothed delay times (in samples) so switching modes glides
    float delayL = 14400.0f;
    float delayR = 14400.0f;

    // Slow modulation phases
    float lfoPhaseA = 0.0f;
    float lfoPhaseB = 0.37f;

    // Feedback damping states
    float shimmerDamp = 0.0f;
    float dampL = 0.0f;
    float dampR = 0.0f;
    float loopDamp = 0.0f;

    unsigned counter = 0;
    int lastMode = -1;
};