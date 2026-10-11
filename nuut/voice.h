
#pragma once

#include "daisysp.h"
#include "transit.h"
#include "resonator.h"

#include <cmath>
#include <cstddef>

using namespace daisysp;

// ============================================================
// SINE WAVETABLE
// ============================================================

constexpr int NUUT_WAVETABLE_SIZE = 2048;

struct NUUTSineWavetable
{
    float table[NUUT_WAVETABLE_SIZE + 1];
};

inline NUUTSineWavetable nuutSineTable;

inline void InitNUUTSineWavetable()
{
    for(int i = 0; i <= NUUT_WAVETABLE_SIZE; ++i)
    {
        nuutSineTable.table[i] =
            sinf(TWOPI_F * static_cast<float>(i)
                 / static_cast<float>(NUUT_WAVETABLE_SIZE));
    }
}

inline float NUUTSine(float phase)
{
    phase -= floorf(phase);

    float index = phase * NUUT_WAVETABLE_SIZE;
    int i = static_cast<int>(index);
    float frac = index - static_cast<float>(i);

    return nuutSineTable.table[i]
         + frac * (nuutSineTable.table[i + 1]
         - nuutSineTable.table[i]);
}

// ============================================================
// HARMONIC AMPLITUDES
// ============================================================

inline void BuildNUUTHarmonics(float* amps)
{
    amps[0] = 0.40f;
    amps[1] = 0.04f;
    amps[2] = 0.015f;
    amps[3] = 0.010f;
    amps[4] = 0.006f;
    amps[5] = 0.003f;
    amps[6] = 0.0015f;
    amps[7] = 0.0f;
}

// ============================================================
// HARMONIC RENDERER
// ============================================================

struct NUUTHarmonicRenderer
{
    float phase[7] = {};
};

inline void InitNUUTHarmonicRenderer(NUUTHarmonicRenderer& renderer)
{
    for(int i = 0; i < 7; ++i)
        renderer.phase[i] = 0.0f;
}

inline float ProcessNUUTHarmonicRenderer(
    NUUTHarmonicRenderer* renderer,
    float frequency,
    const float* amplitudes,
    const float* phases,
    float alchemy,
    float sampleRate)
{
    float out = 0.0f;
    const float inharmonicity = alchemy * 0.12f;

    for(int i = 0; i < 7; ++i)
    {
        float harmonic = static_cast<float>(i + 1);

        harmonic += inharmonicity
                  * static_cast<float>(i)
                  * static_cast<float>(i);

        renderer->phase[i] +=
            (frequency * harmonic) / sampleRate;

        if(renderer->phase[i] >= 1.0f)
            renderer->phase[i] -= 1.0f;

        float phase = renderer->phase[i] + phases[i];
        phase -= floorf(phase);

        out += NUUTSine(phase) * amplitudes[i];
    }

    return out;
}

// ============================================================
// MAIN VOICE
// ============================================================

struct Voice {
    NUUTHarmonicRenderer harmonicRenderer;
    NUUTModalResonator resonator;
    WhiteNoise noise;
    Svf noiseFilter;

    float envelope = 0.0f;
    float frequency = 432.0f;
};

// ============================================================
// SHARED EFFECT STATE
// ============================================================

struct CombLP6BreathState
{
    Oscillator lfo2;
    Comb comb;
    float combBuffer[2048];
};

inline void InitCombLP6Breath(
    CombLP6BreathState* state,
    float sampleRate)
{
    state->lfo2.Init(sampleRate);
    state->lfo2.SetWaveform(Oscillator::WAVE_SIN);
    state->lfo2.SetFreq(0.11f);
    state->lfo2.SetAmp(1.0f);
}

// ============================================================
// EXTRA CONSTELLATION VOICES
// ============================================================

struct PlanetConstellationVoice
{
    Oscillator osc;
    Oscillator fmOsc;

    float envelope = 0.0f;
    float sparkleEnvelope = 0.0f;

    bool active = false;
    bool triggered = false;

    float frequency = 432.0f;
    float targetFrequency = 432.0f;
};

// ============================================================
// INITIALIZATION
// ============================================================

inline void InitVoice(Voice& voice, float sampleRate)
{
    InitNUUTHarmonicRenderer(voice.harmonicRenderer);
    InitNUUTModalResonator(&voice.resonator, sampleRate);

    voice.noise.Init();
    voice.noise.SetAmp(1.0f);

    voice.noiseFilter.Init(48000.0f);
    voice.noiseFilter.SetFreq(2500.0f);
    voice.noiseFilter.SetRes(0.2f);

    voice.envelope = 0.0f;
    voice.frequency = 432.0f;
}

inline void InitPlanetConstellationVoice(
    PlanetConstellationVoice& voice,
    float sampleRate)
{
    voice.osc.Init(sampleRate);
    voice.osc.SetWaveform(Oscillator::WAVE_SIN);
    voice.osc.SetAmp(0.18f);

    voice.fmOsc.Init(sampleRate);
    voice.fmOsc.SetWaveform(Oscillator::WAVE_SIN);
    voice.fmOsc.SetFreq(5.0f);
    voice.fmOsc.SetAmp(1.0f);

    voice.envelope = 0.0f;
    voice.sparkleEnvelope = 0.0f;

    voice.active = false;
    voice.triggered = false;

    voice.frequency = 432.0f;
    voice.targetFrequency = 432.0f;
}

// ============================================================
// MAIN VOICE PROCESSING
// ============================================================

inline float ProcessVoice(
    Voice* voice,
    bool gate,
    float filterMod,
    const AspectData* aspect,
    float originRatio,
    float arcValue,
    float alchemy,
    CombLP6BreathState* state)
{
    const float frequency = voice->frequency * originRatio;

    static const float harmonicAmps[8] = {
        0.40f, 0.04f, 0.015f, 0.010f,
        0.006f, 0.003f, 0.0015f, 0.0f
    };

    float phases[7];

    phases[0] = 0.0f;
    phases[1] = 0.05f * alchemy;
    phases[2] = 0.17f * alchemy;
    phases[3] = 0.31f * alchemy;
    phases[4] = 0.48f * alchemy;
    phases[5] = 0.66f * alchemy;
    phases[6] = 0.82f * alchemy;

    float sig = ProcessNUUTHarmonicRenderer(
        &voice->harmonicRenderer,
        frequency,
        harmonicAmps,
        phases,
        alchemy,
        48000.0f);

    // Crystal Modal Resonator
    sig = ProcessNUUTModalResonator(
            &voice->resonator,
            sig,
            frequency
    );

    sig *= 0.57f;

    voice->noiseFilter.Process(voice->noise.Process());
    const float breathLevel = 0.003f * voice->envelope;
    const float breath = voice->noiseFilter.Low() * breathLevel;
    sig += breath;

    // Envelope
    const float attackTime = 2.2f + arcValue * 4.4f;
    const float releaseTime = 0.8f + arcValue * 2.0f;

    const float attackInc = 1.0f / (attackTime * 48000.0f);
    const float releaseInc = 1.0f / (releaseTime * 48000.0f);

    if(gate)
    {
        voice->envelope += attackInc;

        if(voice->envelope > 1.0f)
            voice->envelope = 1.0f;
    }
    else
    {
        voice->envelope -= releaseInc;

        if(voice->envelope < 0.0f)
            voice->envelope = 0.0f;
    }

    return sig * voice->envelope;
}

// ============================================================
// EXTRA CONSTELLATION VOICE PROCESSING
// ============================================================


inline float ProcessPlanetConstellationVoice(
    PlanetConstellationVoice* voice,
    float targetFrequency)
{
    const float attackIncrement = 1.0f / (0.03f * 48000.0f);
    const float releaseIncrement = 1.0f / (0.30f * 48000.0f);

    // Envelope
    if(voice->active)
    {
        voice->envelope += attackIncrement;

        if(voice->envelope > 1.0f)
            voice->envelope = 1.0f;
    }
    else
    {
        voice->envelope -= releaseIncrement;

        if(voice->envelope < 0.0f)
            voice->envelope = 0.0f;
    }

    // Smooth frequency transition
    voice->frequency +=
        (targetFrequency - voice->frequency) * 0.001f;

    // Oscillator
    voice->osc.SetFreq(voice->frequency);

    float sig = voice->osc.Process();

    // Sparkle trigger
    if(voice->triggered)
    {
        voice->sparkleEnvelope = 1.0f;
        voice->triggered = false;
    }

    if(voice->sparkleEnvelope > 0.0f)
    {
        const float sparkle = voice->fmOsc.Process() * 0.3f;

        sig += sparkle * voice->sparkleEnvelope;

        voice->sparkleEnvelope *= 0.995f;

        if(voice->sparkleEnvelope < 0.001f)
            voice->sparkleEnvelope = 0.0f;
    }

    return sig * voice->envelope * 0.20f;
}
