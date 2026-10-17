#pragma once

#include "daisy_seed.h"
#include "daisysp.h"

struct CrystalStereo
{
    float left;
    float right;
};

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
    // Shared delay
    daisysp::DelayLine<float, 24000> crystalDelay;

    // Ametista: octave shimmer and chimes
    daisysp::PitchShifter crystalShifter;
    daisysp::Oscillator crystalChime;
    daisysp::Oscillator crystalChimeHigh;

    // Shared LFO for slow modulation
    daisysp::Oscillator crystalLfo;

    // Ossidiana: dark resonant filter
    daisysp::Svf crystalFilter;

    // Acquamarina: filtered water noise
    daisysp::Svf aquamarineNoiseFilter;
    daisysp::WhiteNoise aquamarineNoise;

    // General parameters
    float sampleRate = 48000.0f;
    float aquamarineDelayTime = 0.04f;

    // Ametista: sparkle generation
    float sparkleEnvelope = 0.0f;
    uint32_t sparkleCounter = 0;
    uint32_t sparkleRandom = 1234567;
    float chimePan = 0.0f;
};
