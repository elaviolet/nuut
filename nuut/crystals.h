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
    CrystalStereo Process(float input, int mode, float aura, float refraction);

private:
    daisysp::DelayLine<float, 24000> crystalDelay;
    daisysp::PitchShifter crystalShifter;
    daisysp::Oscillator crystalLfo;
    daisysp::Svf crystalFilter;

    float aquamarineDelayTime = 0.18f;
    float pingPongLeft = 0.0f;
    float pingPongRight = 0.0f;
};