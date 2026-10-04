#pragma once

#include "daisysp.h"
#include "Filters/comb.h"

using namespace daisysp;

extern float attackIncrement;
extern float releaseIncrement;


// Aspect types
enum AspectType
{
    ASPECT_NONE = -1,
    ASPECT_CONJUNCTION = 0,
    ASPECT_SEXTILE = 1,
    ASPECT_SQUARE = 2,
    ASPECT_TRINE = 3,
    ASPECT_OPPOSITION = 4
};


struct AspectData
{
    int type = ASPECT_NONE;
    float proximity = 0.0f;
    float intensity = 0.0f;
    float shimmer = 0.0f;
    float tension = 0.0f;
};


// 3 main planets
struct Voice
{
    Oscillator osc1;
    Oscillator osc2;
    Oscillator osc3;
    Oscillator fmOsc;
    Svf filter;

    float envelope = 0.0f;
    bool active = false;
    bool gate = false;

    float frequency = 432.0f;
    float osc2BaseFrequency = 432.0f;
};

struct CombLP6BreathState
{
    Comb comb1;
    Comb comb2;

    Allpass allpass1;
    Allpass allpass2;

    Oscillator lfo1;
    Oscillator lfo2;

    float combBuffer1[2048];
    float combBuffer2[2048];

    float allpassBuffer1[512];
    float allpassBuffer2[512];
};

inline void InitCombLP6Breath(
    CombLP6BreathState* state,
    float sampleRate)
{
    // COMB 1
    state->comb1.Init(
        sampleRate,
        state->combBuffer1,
        2048
    );

    state->comb1.SetRevTime(0.3f);
    state->comb1.SetFreq(22.0f);

    // COMB 2
    state->comb2.Init(
        sampleRate,
        state->combBuffer2,
        2048
    );

    state->comb2.SetRevTime(0.5);
    state->comb2.SetFreq(77.0f);


    // ALLPASS 1
    state->allpass1.Init(
        sampleRate,
        state->allpassBuffer1,
        512
    );

    state->allpass1.SetRevTime(0.5f);
    state->allpass1.SetFreq(0.004f);


    // ALLPASS 2
    state->allpass2.Init(
        sampleRate,
        state->allpassBuffer2,
        512
    );

    state->allpass2.SetRevTime(0.5f);
    state->allpass2.SetFreq(0.006f);


    // LFO 1 → Allpass 1
    state->lfo1.Init(sampleRate);
    state->lfo1.SetWaveform(Oscillator::WAVE_SIN);
    state->lfo1.SetFreq(0.22f);
    state->lfo1.SetAmp(0.5f);

    // LFO 2 → Allpass 2
    state->lfo2.Init(sampleRate);
    state->lfo2.SetWaveform(Oscillator::WAVE_SIN);
    state->lfo2.SetFreq(0.11f);
    state->lfo2.SetAmp(1.0f);
}

// Extra planets
struct PlanetConstellationVoice
{
    Oscillator osc;
    Oscillator fmOsc;

    float envelope = 0.0f;
    float sparkleEnvelope = 0.0f;

    bool active = false;
    bool triggered = false;

    float frequency = 432.0f;
};


inline void InitVoice(Voice& v, float sampleRate)
{
    v.osc1.Init(sampleRate);
    v.osc1.SetWaveform(Oscillator::WAVE_SAW);
    v.osc1.SetFreq(216.0f);
    v.osc1.SetAmp(0.2f);

    v.osc2.Init(sampleRate);
    v.osc2.SetWaveform(Oscillator::WAVE_RAMP);
    v.osc2.SetFreq(432.5f);
    v.osc2.SetAmp(0.2f);

    v.osc3.Init(sampleRate);
    v.osc3.SetWaveform(Oscillator::WAVE_SAW);
    v.osc3.SetFreq(647.0f);
    v.osc3.SetAmp(0.2f);

    v.fmOsc.Init(sampleRate);
    v.fmOsc.SetWaveform(Oscillator::WAVE_SIN);
    v.fmOsc.SetFreq(2.0f);
    v.fmOsc.SetAmp(1.0f);

    v.filter.Init(sampleRate);
    v.filter.SetFreq(1100.0f);
    v.filter.SetRes(0.2f);
}


inline void InitPlanetConstellationVoice(
    PlanetConstellationVoice& v,
    float sampleRate)
{
    v.osc.Init(sampleRate);
    v.osc.SetWaveform(Oscillator::WAVE_SIN);
    v.osc.SetAmp(0.18f);

    v.fmOsc.Init(sampleRate);
    v.fmOsc.SetWaveform(Oscillator::WAVE_SIN);
    v.fmOsc.SetFreq(5.0f);
    v.fmOsc.SetAmp(1.0f);

    v.active = false;
    v.triggered = false;
}


inline float ProcessVoice(
    Voice& v,
    bool gate,
    float filterMod,
    const AspectData& aspect,
    float originRatio,
    float arcValue,
    CombLP6BreathState* state)
{
    // Base harmonic structure
    float osc1Ratio = 0.5f;
    float osc3Ratio = 1.5f;

    v.osc1.SetFreq(
        v.frequency * osc1Ratio * originRatio
    );

    v.osc3.SetFreq(
        v.frequency * osc3Ratio * originRatio
    );

    float sig1 = v.osc1.Process();

    // Base FM amount
    float fmAmount = 8.0f;

    // Aspect transformations
    if(aspect.type == ASPECT_CONJUNCTION)
    {
        // Fusion: increase harmonic density gently.
        fmAmount +=
            aspect.proximity *
            aspect.intensity *
            8.0f;
    }
    else if(aspect.type == ASPECT_SEXTILE)
    {
        // Openness: introduce a brighter upper component.
        float shimmerAmount =
            aspect.proximity *
            aspect.shimmer;

        v.osc3.SetAmp(
            0.2f + shimmerAmount * 0.35f
        );
    }
    else if(aspect.type == ASPECT_SQUARE)
    {
        // Tension: stronger FM and filter movement.
        fmAmount +=
            aspect.proximity *
            aspect.tension *
            18.0f;

        filterMod +=
            aspect.proximity *
            aspect.tension *
            0.35f;
    }
    else if(aspect.type == ASPECT_TRINE)
    {
        // Flow: smoother and more open harmonic movement.
        fmAmount +=
            aspect.proximity *
            aspect.intensity *
            3.0f;

        filterMod +=
            aspect.proximity *
            aspect.shimmer *
            0.25f;
    }
    else if(aspect.type == ASPECT_OPPOSITION)
    {
        // Polarity: controlled detuning and stronger contrast.
        float polarity =
            aspect.proximity *
            aspect.tension;

        v.osc2.SetFreq(
            v.osc2BaseFrequency *
            originRatio *
            (1.0f + polarity * 0.015f)
        );

        fmAmount +=
            polarity * 5.0f;

        filterMod +=
            polarity * 0.25f;
    }

    float fmMod =
        v.fmOsc.Process() * fmAmount;

    v.osc2.SetFreq(
        v.osc2BaseFrequency *
        originRatio +
        fmMod
    );

    float sig2 = v.osc2.Process();
    float sig3 = v.osc3.Process();

    float sig = (sig1 + sig2 + sig3) * 0.15f;

    // Envelope
    float attackTime =
    0.05f + arcValue * 1.5f;

    float releaseTime =
        0.1f + arcValue * 5.0f;

    float attack =
        1.0f /
        (attackTime * 48000.0f);

    float release =
        1.0f /
        (releaseTime * 48000.0f);

    if(gate)
    {
        v.envelope += attack;

        if(v.envelope >= 1.0f)
            v.envelope = 1.0f;
    }
    else
    {
        v.envelope -= release;

        if(v.envelope <= 0.0f)
            v.envelope = 0.0f;
    }

    sig *= v.envelope;

    // Base filter
    float filterFreq =
        500.0f + filterMod * 2500.0f;

    // Aspect-specific filter behavior
    if(aspect.type == ASPECT_CONJUNCTION)
    {
        // Fusion: warmer and denser.
        filterFreq +=
            aspect.proximity *
            aspect.intensity *
            1200.0f;
    }
    else if(aspect.type == ASPECT_SEXTILE)
    {
        // Openness: brighter and more open.
        filterFreq +=
            aspect.proximity *
            aspect.shimmer *
            2500.0f;
    }
    else if(aspect.type == ASPECT_SQUARE)
    {
        // Tension: strong filter movement.
        filterFreq -=
            aspect.proximity *
            aspect.tension *
            1800.0f;
    }
    else if(aspect.type == ASPECT_TRINE)
    {
        // Flow: smooth opening.
        filterFreq +=
            aspect.proximity *
            aspect.shimmer *
            1800.0f;
    }
    else if(aspect.type == ASPECT_OPPOSITION)
    {
        // Polarity: darker and more contrasted.
        filterFreq -=
            aspect.proximity *
            aspect.tension *
            2200.0f;
    }

    if(filterFreq < 150.0f)
        filterFreq = 150.0f;

    if(filterFreq > 14000.0f)
        filterFreq = 14000.0f;

    // Spectral breathe
    // Two independent Comb + Allpass paths.

    float lfo1 = state->lfo1.Process();
    float lfo2 = state->lfo2.Process();


    // Filter 1
    // Allpass frequency modulation: ~0.03
    float allpassTime1 =
        0.004f * (1.0f + lfo1 * 0.03f);

    state->allpass1.SetFreq(allpassTime1);

    float comb1 =
        state->comb1.Process(sig);

    float ap1 =
        state->allpass1.Process(comb1);


    // Filter 2
    // Allpass frequency modulation: ~0.06
    float allpassTime2 =
        0.007f * (1.0f + lfo2 * 0.07f);

    state->allpass2.SetFreq(allpassTime2);

    float comb2 =
        state->comb2.Process(sig);

    float ap2 =
        state->allpass2.Process(comb2);


    // Two separate Allpass amounts
    float filter1 =
        comb1 * 0.45f
        + ap1 * 0.55f;

    float filter2 =
        comb2 * 0.77f
        + ap2 * 0.23f;


    // Combine the two filters
    sig =
        (filter1 * 0.5f
        + filter2 * 0.5f) * 1.5f;

    return sig;

 

}


inline float ProcessPlanetConstellationVoice(
    PlanetConstellationVoice& v,
    float frequency)
{
    const float attackIncrement =
        1.0f / (0.03f * 48000.0f);

    const float releaseIncrement =
        1.0f / (0.30f * 48000.0f);

    if(v.active)
    {
        v.envelope += attackIncrement;

        if(v.envelope > 1.0f)
            v.envelope = 1.0f;
    }
    else
    {
        v.envelope -= releaseIncrement;

        if(v.envelope < 0.0f)
            v.envelope = 0.0f;
    }

    v.osc.SetFreq(frequency);

    float sig = v.osc.Process();

    if(v.triggered)
    {
        v.sparkleEnvelope = 1.0f;
        v.triggered = false;
    }

    if(v.sparkleEnvelope > 0.0f)
    {
        float sparkle =
            v.fmOsc.Process() * 0.3f;

        sig +=
            sparkle * v.sparkleEnvelope;

        v.sparkleEnvelope *= 0.995f;

        if(v.sparkleEnvelope < 0.001f)
            v.sparkleEnvelope = 0.0f;
    }

    return sig * v.envelope * 0.20f;
}