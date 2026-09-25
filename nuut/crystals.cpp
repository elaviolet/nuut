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

CrystalStereo Crystals::Process(float input, int mode, float aura, float refraction)
{

     // Ametista
    if(mode == daisy::Switch3::POS_CENTER)
    {
        float shimmer = crystalShifter.Process(input);
        float shimmerAmount =
            0.35f + aura * 0.50f;
        float delayed = crystalDelay.Read();

        crystalDelay.Write(input + delayed * 0.45f);

        float refractionAmount =
            refraction * 0.5f;
        
        float leftEffect =
            shimmer * (shimmerAmount + refractionAmount)
            + delayed * (0.25f - refractionAmount * 0.5f);

        float rightEffect =
            shimmer * (shimmerAmount - refractionAmount * 0.5f)
            + delayed * (0.25f + refractionAmount);

        return {
            input + leftEffect,
            input + rightEffect
        };
    }

    // Acquamarina
    if(mode == daisy::Switch3::POS_UP)
    {
        float movement = crystalLfo.Process();

        float targetDelayTime =
            0.22f + movement * (0.012f + aura * 0.018f);

        aquamarineDelayTime +=
            (targetDelayTime - aquamarineDelayTime) * 0.002f;

        crystalDelay.SetDelay(
            aquamarineDelayTime * 48000.0f
        );

        float delayed = crystalDelay.Read();

        crystalDelay.Write(
            input + delayed * (0.28f + aura * 0.20f)
        );

        float refractionAmount = refraction * 0.35f;

        float leftEffect =
            input * 0.85f
            + delayed * (0.30f + refractionAmount);

        float rightEffect =
            input * 0.85f
            + delayed * (0.30f - refractionAmount);

        return {
            leftEffect,
            rightEffect
        };
    }

    // Ossidiana
    if(mode == daisy::Switch3::POS_DOWN)
    {
        float delayed = crystalDelay.Read();

        crystalDelay.Write(input + delayed * 0.55f);

        crystalFilter.Process(input + delayed * 0.4f);

        float dark = crystalFilter.Low();

        float refractionAmount = refraction * 0.5f;

        float leftEffect =
            input * (0.65f - refractionAmount * 0.25f)
            + dark * (0.55f + aura * 0.45f + refractionAmount);

        float rightEffect =
            input * (0.65f + refractionAmount)
            + dark * (0.55f + aura * 0.45f - refractionAmount * 0.5f);

        return {
            leftEffect,
            rightEffect
        };
    }

    return {
    input,
    input
};
}