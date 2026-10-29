
#include "crystals.h"

void Crystals::Init(float sampleRate)
{
    this->sampleRate = sampleRate;

    // Shared delay
    crystalDelay.Init();
    crystalDelay.SetDelay(sampleRate * 0.35f);

    // Ametista: octave shimmer
    crystalShifter.Init(sampleRate);
    crystalShifter.SetFun(0.0f);
    crystalShifter.SetTransposition(12.0f);

    // Shared LFO
    crystalLfo.Init(sampleRate);
    crystalLfo.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    crystalLfo.SetFreq(0.15f);
    crystalLfo.SetAmp(1.0f);

    // Ossidiana: dark resonant filter
    crystalFilter.Init(sampleRate);
    crystalFilter.SetFreq(500.0f);
    crystalFilter.SetRes(0.4f);

    // Acquamarina: filtered water noise
    aquamarineNoise.Init();

    aquamarineNoiseFilter.Init(sampleRate);
    aquamarineNoiseFilter.SetFreq(1200.0f);
    aquamarineNoiseFilter.SetRes(0.2f);

    aquamarineDelayTime = 0.04f;
}

CrystalStereo Crystals::Process(
    float input,
    int mode,
    float refraction,
    float radiance,
    bool active)
{
    CrystalStereo result{input, input};

    // AMETISTA: octave shimmer without sparkles
    if(mode == daisy::Switch3::POS_CENTER)
    {
        crystalDelay.SetDelay(sampleRate * 0.35f);

        const float shimmer = crystalShifter.Process(input);

        const float delayed = crystalDelay.Read();

        crystalDelay.Write(shimmer * 0.7f + delayed * 0.30f);

        const float refractionAmount = refraction * 0.5f;

        const float leftEffect =
            shimmer * (0.175f + refractionAmount)
            + delayed * (0.25f - refractionAmount * 0.5f);

        const float rightEffect =
            shimmer * (0.175f - refractionAmount * 0.5f)
            + delayed * (0.25f + refractionAmount);

        result = {
            input + leftEffect,
            input + rightEffect
        };
    }

    // ACQUAMARINA: filtered water noise
    else if(mode == daisy::Switch3::POS_UP)
    {
        const float movement = crystalLfo.Process();

        // Slowly modulated water-noise envelope
        const float wave = 0.5f + 0.5f * movement;
        float noiseEnvelope = wave * wave;
        noiseEnvelope *= noiseEnvelope;

        const float noise = aquamarineNoise.Process();
        aquamarineNoiseFilter.Process(noise);

        const float filteredNoise = aquamarineNoiseFilter.Low();

        const float noiseAmount =
            (0.001f + radiance * 0.002f) * noiseEnvelope;

        const float noiseSignal =
            active ? filteredNoise * noiseAmount : 0.0f;

        // Gentle aquatic delay movement
        const float targetDelayTime =
            0.040f + movement * 0.003f;

        aquamarineDelayTime +=
            (targetDelayTime - aquamarineDelayTime) * 0.001f;

        crystalDelay.SetDelay(aquamarineDelayTime * sampleRate);

        const float delayed = crystalDelay.Read();
        const float feedback = 0.18f + radiance * 0.08f;

        crystalDelay.Write(
            input + noiseSignal + delayed * feedback
        );

        const float refractionAmount = refraction * 0.25f;

        result = {
            input * 0.85f
                + delayed * (0.30f + refractionAmount)
                + noiseSignal,

            input * 0.85f
                + delayed * (0.30f - refractionAmount)
                + noiseSignal
        };
    }

    // OSSIDIANA: dark resonant filter
    else if(mode == daisy::Switch3::POS_DOWN)
    {
        crystalDelay.SetDelay(sampleRate * 0.35f);

        const float delayed = crystalDelay.Read();
        const float feedback = 0.55f + radiance * 0.12f;

        crystalDelay.Write(input + delayed * feedback);

        crystalFilter.Process(
            input + delayed * (0.4f + radiance * 0.15f)
        );

        const float dark = crystalFilter.Low();
        const float refractionAmount = refraction * 0.5f;
        const float resonance = 0.55f * 0.45f + radiance * 0.25f;

        result = {
            input * (0.65f - refractionAmount * 0.25f)
                + dark * (resonance + refractionAmount),

            input * (0.65f + refractionAmount)
                + dark * (resonance - refractionAmount * 0.5f)
        };
    }

    return result;
}
