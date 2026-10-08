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
    crystalChime.Init(sampleRate);
    crystalChime.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    crystalChime.SetFreq(2222.2f);
    crystalChime.SetAmp(1.0f);
    crystalChimeHigh.Init(sampleRate);
    crystalChimeHigh.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    crystalChimeHigh.SetFreq(6200.0f);
    crystalChimeHigh.SetAmp(0.35f);
    crystalFilter.Init(sampleRate);
    crystalFilter.SetFreq(500.0f);
    crystalFilter.SetRes(0.4f);
}

CrystalStereo Crystals::Process(float input, int mode, float refraction, float radiance, bool active)
{
    // Ametista
    if(mode == daisy::Switch3::POS_CENTER)
    {
        float shimmer = crystalShifter.Process(input);

        // radiance sparkles
        sparkleCounter++;

        if(radiance > 0.05f && sparkleCounter > 1800 && active)
        {
            sparkleRandom =
                sparkleRandom * 1664525u + 1013904223u;

            uint32_t chance = sparkleRandom % 100;

            if(chance < static_cast<uint32_t>(radiance * 35.0f))
            {
                sparkleEnvelope = 0.85f;

                chimePan =
                    (static_cast<float>(sparkleRandom % 200) / 100.0f) - 1.0f;
            }

            sparkleCounter = 0;
        }

        float chime =
            (crystalChime.Process()
            + crystalChimeHigh.Process())
            * sparkleEnvelope;

        sparkleEnvelope *= 0.99995f;

        if(sparkleEnvelope < 0.001f)
            sparkleEnvelope = 0.0f;

        float shimmerAmount =
            0.35f * 0.50f;

        float delayed = crystalDelay.Read();

        crystalDelay.Write(
            shimmer * 0.7f + delayed * 0.65f
        );

        float refractionAmount =
            refraction * 0.5f;

        float chimeAmount =
            chime * radiance * 0.045f;

        float panAmount =
            chimePan * refraction;

        float chimeLeft =
            chimeAmount * (1.0f - panAmount);

        float chimeRight =
            chimeAmount * (1.0f + panAmount);

        float leftEffect =
            shimmer * (shimmerAmount + refractionAmount)
            + delayed * (0.25f - refractionAmount * 0.5f)
            + chimeLeft;

        float rightEffect =
            shimmer * (shimmerAmount - refractionAmount * 0.5f)
            + delayed * (0.25f + refractionAmount)
            + chimeRight;

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
            0.22f + movement * (0.012f  * 0.018f);

        aquamarineDelayTime +=
            (targetDelayTime - aquamarineDelayTime) * 0.002f;

        crystalDelay.SetDelay(
            aquamarineDelayTime * 48000.0f
        );

        float delayed = crystalDelay.Read();

        float feedback =
            0.28f
             * 0.20f
            + radiance * 0.12f;

        crystalDelay.Write(
            input + delayed * feedback
        );

        float refractionAmount = refraction * 0.35f;

        float radianceMovement =
            movement * radiance * 0.12f;

        float leftEffect =
            input * 0.85f
            + delayed * (0.30f + refractionAmount)
            + radianceMovement;

        float rightEffect =
            input * 0.85f
            + delayed * (0.30f - refractionAmount)
            - radianceMovement;

        return {
            leftEffect,
            rightEffect
        };
    }
    
    // Ossidiana
    if(mode == daisy::Switch3::POS_DOWN)
    {
        float delayed = crystalDelay.Read();

        float feedback =
            0.55f + radiance * 0.12f;

        crystalDelay.Write(
            input + delayed * feedback
        );

        crystalFilter.Process(
            input + delayed * (0.4f + radiance * 0.15f)
        );

        float dark = crystalFilter.Low();

        float refractionAmount = refraction * 0.5f;

        float resonance =
            0.55f
             * 0.45f
            + radiance * 0.25f;

        float leftEffect =
            input * (0.65f - refractionAmount * 0.25f)
            + dark * (resonance + refractionAmount);

        float rightEffect =
            input * (0.65f + refractionAmount)
            + dark * (resonance - refractionAmount * 0.5f);

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