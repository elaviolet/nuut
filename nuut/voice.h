
#pragma once

#include "daisysp.h"
#include "transit.h"
#include "resonator.h"

#include <cmath>
#include <cstddef>

using namespace daisysp;


// ============================================================
// MAIN VOICE
// ============================================================

struct Voice {
    Oscillator osc;
    Oscillator triOsc;
    NUUTModalResonator resonator;

    Svf alchemyFilter;
    float alchemyLfoPhase = 0.0f;

    float envelope = 0.0f;
    float frequency = 432.0f;
    float smoothedFrequency = 432.0f;
    float sampleRate = 48000.0f;
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

    state->comb.Init(sampleRate, state->combBuffer, 2048);
    state->comb.SetFreq(600.0f);
}

// ============================================================
// EXTRA CONSTELLATION VOICES
// ============================================================

struct PlanetConstellationVoice
{
    Oscillator osc;

    float envelope = 0.0f;

    bool active = false;

    float frequency = 432.0f;
    float targetFrequency = 432.0f;
};

// ============================================================
// INITIALIZATION
// ============================================================


inline void InitVoice(Voice* voice, float sampleRate)
{
    voice->osc.Init(sampleRate);
    voice->osc.SetWaveform(Oscillator::WAVE_SIN);
    voice->osc.SetAmp(0.40f);
    voice->osc.SetFreq(432.0f);

    voice->triOsc.Init(sampleRate);
    voice->triOsc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
    voice->triOsc.SetAmp(0.05f);
    
    InitNUUTModalResonator(&voice->resonator, sampleRate);

    voice->alchemyFilter.Init(sampleRate);
    voice->alchemyFilter.SetFreq(2000.0f);
    voice->alchemyFilter.SetRes(0.9f);

    voice->alchemyLfoPhase = 0.0f;
    voice->envelope = 0.0f;
    voice->frequency = 432.0f;
    voice->smoothedFrequency = 432.0f;
}


inline void InitPlanetConstellationVoice(
    PlanetConstellationVoice& voice,
    float sampleRate)
{
    voice.osc.Init(sampleRate);
    voice.osc.SetWaveform(Oscillator::WAVE_SIN);
    voice.osc.SetAmp(0.18f);

    voice.envelope = 0.0f;
    voice.active = false;

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
    float kosmos, int voiceIndex,
    float rootFrequency,
    CombLP6BreathState* state)
{
    
    // KOSMOS — smooth harmony transitions
    const float kosmosAmount = fmaxf(0.0f, fminf(1.0f, kosmos));

    const float kosmosPosition = kosmosAmount * 4.0f;
    const int harmonyMode = (int)kosmosPosition;
    const float harmonyBlend = kosmosPosition - harmonyMode;

    const float ratioVoice1Table[5] = {
        1.25f,       // Major
        1.125f,      // Sus2
        1.333333f,   // Sus4
        1.2f,        // Minor
        1.5f         // Power chord
    };

    const float ratioVoice2Table[5] = {
        1.5f,
        1.5f,
        1.5f,
        1.5f,
        2.0f
    };

    const int nextMode = (harmonyMode < 4)
        ? harmonyMode + 1
        : harmonyMode;

    const float ratioVoice1 =
        ratioVoice1Table[harmonyMode] +
        (ratioVoice1Table[nextMode] - ratioVoice1Table[harmonyMode])
        * harmonyBlend;

    const float ratioVoice2 =
        ratioVoice2Table[harmonyMode] +
        (ratioVoice2Table[nextMode] - ratioVoice2Table[harmonyMode])
        * harmonyBlend;

    float harmonicRatio = 1.0f;

    if(voiceIndex == 1)
        harmonicRatio = ratioVoice1;
    else if(voiceIndex == 2)
        harmonicRatio = ratioVoice2;

    const float targetFrequency =
    voice->frequency * originRatio * harmonicRatio;

    const float envelopeAmount = voice->envelope;

    const float frequencySmoothing =
        0.0005f + envelopeAmount * 0.0045f;

    voice->smoothedFrequency +=
        (targetFrequency - voice->smoothedFrequency) * frequencySmoothing;

    const float frequency = voice->smoothedFrequency;

    // ALCHEMY — slow spectral movement
    const float a = fmaxf(0.0f, fminf(1.0f, alchemy));

    voice->alchemyLfoPhase += 0.07f / voice->sampleRate;
    if(voice->alchemyLfoPhase >= 1.0f)
        voice->alchemyLfoPhase -= 1.0f;

    const float p = voice->alchemyLfoPhase;
    const float lfo = (p < 0.5f)
        ? (p * 4.0f - 1.0f)
        : (3.0f - p * 4.0f);

   const float morph = fmaxf(0.0f, fminf(
        1.0f,
        a + lfo * 0.45f * a
    ));

    voice->osc.SetFreq(frequency);
    voice->triOsc.SetFreq(frequency * 0.5f);

    float sine = voice->osc.Process();
    float triangle = voice->triOsc.Process();

    float sig = sine + triangle;


    // Crystal Modal Resonator
    sig = ProcessNUUTModalResonator(
        &voice->resonator,
        sig,
        frequency,
        morph
    );
    
   // ALCHEMY — filter + comb morph
    const float dry = sig;

    voice->alchemyFilter.Process(sig);
    const float filtered = voice->alchemyFilter.Low();

    const float combInput = dry * (1.0f - a) + filtered * a;
    const float combOutput = state->comb.Process(combInput * 0.5f);
    const float combMix = a * 0.25f;

    sig = combInput * (1.0f - combMix)
        + combOutput * combMix;

    sig *= 0.45f;
 

    // Envelope
    const float sr = voice->sampleRate;

    const float attackTime  = 0.03f + arcValue * 1.20f;
    const float releaseTime = 0.15f + arcValue * 3.00f;

    const float attackInc  = 1.0f / (attackTime * sr);
    const float releaseInc = 1.0f / (releaseTime * sr);

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

    
    voice->frequency = targetFrequency;

    // Oscillator
    voice->osc.SetFreq(voice->frequency);

    float sig = voice->osc.Process();


    return sig * voice->envelope * 0.20f;
}
