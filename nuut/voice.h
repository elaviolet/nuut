#pragma once

#include "daisysp.h"

using namespace daisysp;

// Parametri globali dell'envelope, definiti in nuut.cpp
extern float attackIncrement;
extern float releaseIncrement;

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

inline float ProcessVoice(
    Voice& v,
    bool gate,
    float filterMod,
    float oppositionProximity
)
{
    float sig1 = v.osc1.Process();

    float fmAmount = 8.0f;

    if(oppositionProximity > 0.0f)
    {
        fmAmount += oppositionProximity * 160.0f;
    }

float fmMod = v.fmOsc.Process() * fmAmount;

    v.osc2.SetFreq(v.osc2BaseFrequency + fmMod);

    float sig2 = v.osc2.Process();
    float sig3 = v.osc3.Process();

    float sig = (sig1 + sig2 + sig3) * 0.25f;

    // Envelope
    if(gate)
    {
        v.envelope += attackIncrement;

        if(v.envelope >= 1.0f)
            v.envelope = 1.0f;
    }
    else
    {
        v.envelope -= releaseIncrement;

        if(v.envelope <= 0.0f)
            v.envelope = 0.0f;
    }

    sig *= v.envelope;

    // Filter
    float filterFreq = 700.0f + filterMod * 600.0f;

    if(filterFreq < 100.0f)
        filterFreq = 100.0f;

    v.filter.SetFreq(filterFreq);
    v.filter.Process(sig);

    sig = v.filter.Low();

    return sig;
}