#pragma once

#include "daisy_seed.h"
#include "daisysp.h"

class Crystals
{
public:
    void Init(float sampleRate);
    float Process(float input, int mode);

private:
    daisysp::DelayLine<float, 24000> crystalDelay;
    daisysp::PitchShifter crystalShifter;
    daisysp::Oscillator crystalLfo;
    daisysp::Svf crystalFilter;
};