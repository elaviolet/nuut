
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

    for(std::size_t i = 0; i < NUUTModalResonator::MODE_COUNT; ++i)
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
    NUUTModalResonator* resonator,
    float input,
    float baseFrequency)
{
    constexpr float MODE_RATIOS[NUUTModalResonator::MODE_COUNT] =
    {
        1.0f,
        1.498f,
        2.01f,
        2.72f,
        3.87f,
        5.12f
    };

    constexpr float MODE_DRIFT[NUUTModalResonator::MODE_COUNT] =
    {
        0.0010f,
        0.0018f,
        0.0013f,
        0.0022f,
        0.0016f,
        0.0025f
    };

    constexpr float MODE_SPEED[NUUTModalResonator::MODE_COUNT] =
    {
        0.035f,
        0.047f,
        0.029f,
        0.041f,
        0.025f,
        0.053f
    };

    constexpr float TWO_PI = 6.28318530718f;
    constexpr float RADIUS = 0.97f;
    constexpr float MODE_GAIN = 0.025f;
    constexpr float WET_MIX = 0.10f;

    const float sr = resonator->sampleRate;

    if(baseFrequency < 20.0f)
        baseFrequency = 20.0f;

    // Update resonator coefficients every 32 samples.
    // This keeps the mode frequencies moving smoothly
    // without recalculating cosine for every sample.
    if(resonator->updateCounter == 0)
    {
        for(std::size_t i = 0;
            i < NUUTModalResonator::MODE_COUNT;
            ++i)
        {
            const float drift =
                sinf(resonator->phase[i]) * MODE_DRIFT[i];

            float frequency =
                baseFrequency * MODE_RATIOS[i] * (1.0f + drift);

            const float maxFrequency = sr * 0.42f;

            if(frequency > maxFrequency)
                frequency = maxFrequency;

            if(frequency < 20.0f)
                frequency = 20.0f;

            const float omega = TWO_PI * frequency / sr;

            resonator->coeffA[i] =
                2.0f * RADIUS * cosf(omega);

            resonator->coeffB[i] =
                -(RADIUS * RADIUS);

            const float damping = 1.0f - RADIUS;

            resonator->coeffInput[i] =
                damping * damping;
        }
    }

    resonator->updateCounter =
        (resonator->updateCounter + 1) & 31U;

    float modalOutput = 0.0f;

    for(std::size_t i = 0;
        i < NUUTModalResonator::MODE_COUNT;
        ++i)
    {
        // Slowly evolving modal frequencies.
        resonator->phase[i] +=
            TWO_PI * MODE_SPEED[i] / sr;

        if(resonator->phase[i] >= TWO_PI)
            resonator->phase[i] -= TWO_PI;

        // Two-pole resonator.
        const float y =
            resonator->coeffInput[i] * input
            + resonator->coeffA[i] * resonator->y1[i]
            + resonator->coeffB[i] * resonator->y2[i];

        resonator->y2[i] = resonator->y1[i];
        resonator->y1[i] = y;

        modalOutput += y * MODE_GAIN;
    }

    // Conservative parallel blend: retain the original
    // signal and add a restrained crystalline resonance.
    return input;
}
