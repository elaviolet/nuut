#include "crystals.h"

void Crystals::Init(float sampleRate)
{
    crystalDelay.Init();
    crystalDelay.SetDelay(sampleRate * 0.35f);
    crystalShifter.Init(sampleRate);
    crystalShifter.SetTransposition(12.0f);  
    crystalLfo.Init(sampleRate);
    crystalLfo.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    crystalLfo.SetFreq(0.15f);
    crystalLfo.SetAmp(1.0f);
    crystalFilter.Init(sampleRate);
    crystalFilter.SetFreq(500.0f);
    crystalFilter.SetRes(0.4f);
}

float Crystals::Process(float input, int mode, float wet)
{

     // Ametista
    if(mode == daisy::Switch3::POS_CENTER)
    {
        float shimmer = crystalShifter.Process(input);
        float delayed = crystalDelay.Read();

        crystalDelay.Write(input + delayed * 0.45f);

        float effect =
            input + shimmer * 0.35f + delayed * 0.25f;

        return input * (1.0f - wet) + effect * wet;
    }

    // Acquamarina
    if(mode == daisy::Switch3::POS_UP)
    {

        float movement = crystalLfo.Process();

        float delayTime =
            0.18f + movement * 0.06f;

        crystalDelay.SetDelay(delayTime * 48000.0f);

        float delayed = crystalDelay.Read();

        crystalDelay.Write(input + delayed * 0.35f);

        float effect = input + delayed * 0.35f;

        return input * (1.0f - wet) + effect * wet;
    }

    // Ossidiana
    if(mode == daisy::Switch3::POS_DOWN)
    {
        float delayed = crystalDelay.Read();

        crystalDelay.Write(input + delayed * 0.55f);

        crystalFilter.Process(input + delayed * 0.4f);

        float dark = crystalFilter.Low();

        float effect = input * 0.65f + dark * 0.55f;

        return input * (1.0f - wet) + effect * wet;
    }

    return input;
}