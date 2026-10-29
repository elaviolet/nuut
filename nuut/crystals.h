
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

    // Ametista: octave shimmer
    daisysp::PitchShifter crystalShifter;

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
};
