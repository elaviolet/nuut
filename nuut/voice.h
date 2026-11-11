#pragma once

#include "daisysp.h"
#include "transit.h"
#include "resonator.h"

#include <cmath>
#include <cstddef>

using namespace daisysp;


// ============================================================
// HELPERS
// ============================================================

// Cheap sine for a phase in [0, 1). Parabolic approximation with one
// refinement step (error around 0.1%, inaudible on a pad voice).
// Much cheaper than sinf().
inline float FastSin01(float phase)
{
    const float x = 2.0f * phase - 1.0f;
    float y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}

// ============================================================
// MAIN VOICE
// ============================================================

struct Voice {
    Oscillator osc;
    Oscillator triOsc;
    NUUTModalResonator resonator;

    // Not used any more: the Alchemy filter + comb now live in the
    // global EtherealStage. Kept so other files still compile.
    Svf alchemyFilter;

    float alchemyLfoPhase = 0.0f;
    float alchemyLfoInc = 0.07f / 48000.0f;

    float envelope = 0.0f;
    float frequency = 432.0f;
    float smoothedFrequency = 432.0f;
    float sampleRate = 48000.0f;

    // Kosmos harmony ratio cache: it only changes when the knob moves,
    // so it is not recomputed for every sample.
    float cachedKosmos = -1.0f;
    int cachedVoiceIndex = -1;
    float cachedHarmonicRatio = 1.0f;

    // Waveform morph: own phase accumulator, smoothed morph amount.
    float wavePhase = 0.0f;
    float morphSmoothed = 0.0f;
};


// ============================================================
// LEGACY SHARED EFFECT STATE
// Not used by the main voices any more (the comb is global now).
// Kept so other files that reference it still compile.
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
    // Kept so other files that reference it still compile;
    // the voice uses its own phase accumulator (see below).
    Oscillator osc;

    float envelope = 0.0f;

    bool active = false;

    float frequency = 432.0f;
    float targetFrequency = 432.0f;

    float phase = 0.0f;
    float invSampleRate = 1.0f / 48000.0f;
    float attackIncrement = 1.0f / (0.03f * 48000.0f);
    float releaseIncrement = 1.0f / (0.30f * 48000.0f);
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

    voice->sampleRate = sampleRate;
    voice->alchemyLfoInc = 0.07f / sampleRate;
    voice->alchemyLfoPhase = 0.0f;
    voice->envelope = 0.0f;
    voice->frequency = 432.0f;
    voice->smoothedFrequency = 432.0f;

    voice->cachedKosmos = -1.0f;
    voice->cachedVoiceIndex = -1;
    voice->cachedHarmonicRatio = 1.0f;
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

    voice.phase = 0.0f;
    voice.invSampleRate = 1.0f / sampleRate;
    voice.attackIncrement = 1.0f / (0.03f * sampleRate);
    voice.releaseIncrement = 1.0f / (0.30f * sampleRate);
}

// ============================================================
// KOSMOS — smooth harmony transitions
// ============================================================

inline float ComputeHarmonicRatio(float kosmos, int voiceIndex)
{
    if(voiceIndex != 1 && voiceIndex != 2)
        return 1.0f;

    static const float ratioVoice1Table[5] = {
        1.25f,       // Major
        1.125f,      // Sus2
        1.333333f,   // Sus4
        1.2f,        // Minor
        1.5f         // Power chord
    };

    static const float ratioVoice2Table[5] = {
        1.5f,
        1.5f,
        1.5f,
        1.5f,
        2.0f
    };

    const float kosmosAmount = fmaxf(0.0f, fminf(1.0f, kosmos));
    const float kosmosPosition = kosmosAmount * 4.0f;
    const int harmonyMode = (int)kosmosPosition;
    const float harmonyBlend = kosmosPosition - harmonyMode;

    const int nextMode = (harmonyMode < 4)
        ? harmonyMode + 1
        : harmonyMode;

    const float* table = (voiceIndex == 1)
        ? ratioVoice1Table
        : ratioVoice2Table;

    return table[harmonyMode]
        + (table[nextMode] - table[harmonyMode]) * harmonyBlend;
}

// ============================================================
// MAIN VOICE PROCESSING
//
// The voice is now just oscillators + modal resonator + envelope.
// The Alchemy filter/comb/echo are applied globally, after the
// voices are summed (see EtherealStage), so a voice whose gate
// is closed and whose envelope has reached zero costs nothing.
// The signature is unchanged; filterMod, aspect, rootFrequency
// and state are unused.
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
    CombLP6BreathState* state,
    float waveMorph = 0.0f)
{
    // Silent voice: nothing audible, skip all DSP.
    if(!gate && voice->envelope <= 0.0f)
        return 0.0f;

    // KOSMOS — recomputed only when the knob (or voice index) changes
    if(voice->cachedVoiceIndex != voiceIndex
       || voice->cachedKosmos != kosmos)
    {
        voice->cachedHarmonicRatio = ComputeHarmonicRatio(kosmos, voiceIndex);
        voice->cachedKosmos = kosmos;
        voice->cachedVoiceIndex = voiceIndex;
    }
    const float harmonicRatio = voice->cachedHarmonicRatio;

    const float targetFrequency =
        voice->frequency * originRatio * harmonicRatio;

    const float envelopeAmount = voice->envelope;

    const float frequencySmoothing =
        0.0005f + envelopeAmount * 0.0045f;

    voice->smoothedFrequency +=
        (targetFrequency - voice->smoothedFrequency) * frequencySmoothing;

    const float frequency = voice->smoothedFrequency;

    // ALCHEMY — slow spectral movement of the resonator
    const float a = fmaxf(0.0f, fminf(1.0f, alchemy));

    voice->alchemyLfoPhase += voice->alchemyLfoInc;
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

    voice->triOsc.SetFreq(frequency * 0.5f);

    // WAVEFORM MORPH: 0 = sine, 0.5 = triangle, 1 = sawtooth.
    // One phase accumulator feeds all three shapes. The morph amount is
    // slewed per sample, so moving the fader never clicks.
    voice->morphSmoothed +=
        0.002f * (fmaxf(0.0f, fminf(1.0f, waveMorph)) - voice->morphSmoothed);
    const float m = voice->morphSmoothed;

    const float dt = frequency * (1.0f / voice->sampleRate);
    voice->wavePhase += dt;
    if(voice->wavePhase >= 1.0f)
        voice->wavePhase -= 1.0f;
    const float ph = voice->wavePhase;

    float wave = FastSin01(ph);
    if(m > 0.001f)
    {
        // Triangle, phase-aligned with the sine (little aliasing, so it
        // is left naive).
        float q = ph + 0.25f;
        if(q >= 1.0f)
            q -= 1.0f;
        const float tri = 1.0f - 4.0f * fabsf(q - 0.5f);
        // Sawtooth with a PolyBLEP correction at its jump. Its phase is
        // shifted by half a turn so that it rises through zero together
        // with the sine.
        float ps = ph + 0.5f;
        if(ps >= 1.0f)
            ps -= 1.0f;
        float saw = 2.0f * ps - 1.0f;
        if(ps < dt)
        {
            const float t = ps / dt;
            saw -= t + t - t * t - 1.0f;
        }
        else if(ps > 1.0f - dt)
        {
            const float t = (ps - 1.0f) / dt;
            saw -= t * t + t + t + 1.0f;
        }

        // Levels matched to the sine (the saw is brighter, so lower).
        if(m < 0.5f)
            wave += (tri * 0.90f - wave) * (m * 2.0f);
        else
            wave = tri * 0.90f + (saw * 0.40f - tri * 0.90f) * (m * 2.0f - 1.0f);
    }

    const float sine = wave * 0.40f;
    float triangle = voice->triOsc.Process();

    float sig = sine + triangle;

    // Crystal Modal Resonator
    sig = ProcessNUUTModalResonator(
        &voice->resonator,
        sig,
        frequency,
        morph
    );

    sig *= 0.45f;

    // Envelope
    const float sr = voice->sampleRate;

    if(gate)
    {
        const float attackTime = 0.03f + arcValue * 1.20f;
        voice->envelope += 1.0f / (attackTime * sr);

        if(voice->envelope > 1.0f)
            voice->envelope = 1.0f;
    }
    else
    {
        const float releaseTime = 0.15f + arcValue * 3.00f;
        voice->envelope -= 1.0f / (releaseTime * sr);

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
    // Silent and not active: nothing to compute.
    if(!voice->active && voice->envelope <= 0.0f)
        return 0.0f;

    // Envelope
    if(voice->active)
    {
        voice->envelope += voice->attackIncrement;

        if(voice->envelope > 1.0f)
            voice->envelope = 1.0f;
    }
    else
    {
        voice->envelope -= voice->releaseIncrement;

        if(voice->envelope < 0.0f)
            voice->envelope = 0.0f;
    }

    voice->frequency = targetFrequency;

    // Sine oscillator (own phase accumulator + fast sine)
    voice->phase += voice->frequency * voice->invSampleRate;
    if(voice->phase >= 1.0f)
        voice->phase -= 1.0f;

    // 0.18 oscillator amplitude * 0.20 output gain, as in the original.
    return FastSin01(voice->phase) * voice->envelope * (0.18f * 0.20f);
}