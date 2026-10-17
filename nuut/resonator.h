#pragma once

#include <cmath>
#include <cstddef>

// NUUT CRYSTAL MODAL RESONATOR
//
// Six resonant modes with independent state.
// Each mode follows the played pitch, with a slightly
// inharmonic frequency ratio and slow continuous drift.
//
// This module is independent from the Harmonic Engine
// and the Crystal Engine.

struct NUUTModalResonator
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float phase[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};
    float coeffInput[MODE_COUNT] = {};

    std::size_t updateCounter = 0;
    float sampleRate = 48000.0f;
};

inline void InitNUUTModalResonator(
    NUUTModalResonator* resonator,
    float sampleRate)
{
    if(sampleRate <= 0.0f)
        sampleRate = 48000.0f;

    resonator->sampleRate = sampleRate;
    resonator->updateCounter = 0;

    for(std::size_t i = 0;
        i < NUUTModalResonator::MODE_COUNT;
        ++i)
    {
        resonator->y1[i] = 0.0f;
        resonator->y2[i] = 0.0f;
        resonator->phase[i] = 0.0f;

        resonator->coeffA[i] = 0.0f;
        resonator->coeffB[i] = 0.0f;
        resonator->coeffInput[i] = 0.0f;
    }
}


inline float ProcessNUUTModalResonator(
    NUUTModalResonator* r,
    float input,
    float baseFrequency,
    float alchemy = 0.0f)
{
    static constexpr float MODE_RATIOS[NUUTModalResonator::MODE_COUNT] =
    {
        1.0f, 1.498f, 2.01f, 2.72f, 3.87f, 5.12f
    };

    static constexpr float MODE_DRIFT[NUUTModalResonator::MODE_COUNT] =
    {
        0.0010f, 0.0018f, 0.0013f,
        0.0022f, 0.0016f, 0.0025f
    };

    static constexpr float MODE_SPEED[NUUTModalResonator::MODE_COUNT] =
    {
        0.035f, 0.047f, 0.029f,
        0.041f, 0.025f, 0.053f
    };

    constexpr float TWO_PI = 6.28318530718f;
    constexpr float RADIUS = 0.97f;
    constexpr float MODE_GAIN = 0.12f;
    constexpr float WET_MIX = 0.65f;

    const float a = fmaxf(0.0f, fminf(1.0f, alchemy));
    const float sr = r->sampleRate;

    if(baseFrequency < 20.0f)
        baseFrequency = 20.0f;

    // Refresh the resonator coefficients every 64 samples.
    // The modal frequency drift continues at the original rate.
    if(r->updateCounter == 0)
    {
        const float maxFrequency = sr * 0.42f;
        const float damping = 1.0f - RADIUS;
        const float inputCoeff = damping * damping;

        for(std::size_t i = 0;
            i < NUUTModalResonator::MODE_COUNT;
            ++i)
        {
            const float drift = sinf(r->phase[i]) * MODE_DRIFT[i];

            float frequency =
                baseFrequency * MODE_RATIOS[i] * (1.0f + drift);

            if(frequency > maxFrequency)
                frequency = maxFrequency;

            if(frequency < 20.0f)
                frequency = 20.0f;

            const float omega = TWO_PI * frequency / sr;

            r->coeffA[i] = 2.0f * RADIUS * cosf(omega);
            r->coeffB[i] = -(RADIUS * RADIUS);
            r->coeffInput[i] = inputCoeff;
        }
    }

    r->updateCounter = (r->updateCounter + 1U) & 63U;

    float modalOutput = 0.0f;

    for(std::size_t i = 0;
        i < NUUTModalResonator::MODE_COUNT;
        ++i)
    {
        r->phase[i] += TWO_PI * MODE_SPEED[i] / sr;

        if(r->phase[i] >= TWO_PI)
            r->phase[i] -= TWO_PI;

        const float y =
            r->coeffInput[i] * input
            + r->coeffA[i] * r->y1[i]
            + r->coeffB[i] * r->y2[i];

        r->y2[i] = r->y1[i];
        r->y1[i] = y;

        float weight;

        switch(i)
        {
            case 0: weight = 1.00f; break;
            case 1: weight = 0.80f + 0.20f * a; break;
            case 2: weight = 0.60f + 0.40f * a; break;
            case 3: weight = 0.42f + 0.48f * a; break;
            case 4: weight = 0.28f + 0.52f * a; break;
            default: weight = 0.18f + 0.57f * a; break;
        }

        modalOutput += y * MODE_GAIN * weight;
    }

    return input + modalOutput * WET_MIX;
}
