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

    // weight = BASE + SLOPE * alchemy (replaces the per-sample switch)
    static constexpr float WEIGHT_BASE[NUUTModalResonator::MODE_COUNT] =
    {
        1.00f, 0.80f, 0.60f, 0.42f, 0.28f, 0.18f
    };

    static constexpr float WEIGHT_SLOPE[NUUTModalResonator::MODE_COUNT] =
    {
        0.00f, 0.20f, 0.40f, 0.48f, 0.52f, 0.57f
    };

    constexpr float TWO_PI = 6.28318530718f;
    constexpr float RADIUS = 0.97f;
    constexpr float MODE_GAIN = 0.12f;
    constexpr float WET_MIX = 0.65f;
    constexpr float UPDATE_PERIOD = 64.0f;

    const float a = fmaxf(0.0f, fminf(1.0f, alchemy));
    const float sr = r->sampleRate;

    if(baseFrequency < 20.0f)
        baseFrequency = 20.0f;

    // Refresh the coefficients every 64 samples.
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

            // The drift phase is only read here, so advance it by a whole
            // update period at once (same result as 64 per-sample steps).
            r->phase[i] += TWO_PI * MODE_SPEED[i] / sr * UPDATE_PERIOD;

            if(r->phase[i] >= TWO_PI)
                r->phase[i] -= TWO_PI;
        }
    }

    r->updateCounter = (r->updateCounter + 1U) & 63U;

    float modalOutput = 0.0f;

    for(std::size_t i = 0;
        i < NUUTModalResonator::MODE_COUNT;
        ++i)
    {
        const float y =
            r->coeffInput[i] * input
            + r->coeffA[i] * r->y1[i]
            + r->coeffB[i] * r->y2[i];

        r->y2[i] = r->y1[i];
        r->y1[i] = y;

        modalOutput +=
            y * MODE_GAIN * (WEIGHT_BASE[i] + WEIGHT_SLOPE[i] * a);
    }

    return input + modalOutput * WET_MIX;
}


struct NUUTEventBell
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};
    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};

    float sampleRate = 48000.0f;
};

inline void InitNUUTEventBell(
    NUUTEventBell* bell,
    float sampleRate)
{
    bell->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
    {
        bell->y1[i] = 0.0f;
        bell->y2[i] = 0.0f;
        bell->coeffA[i] = 0.0f;
        bell->coeffB[i] = 0.0f;
    }
}

inline float ProcessNUUTEventBell(
    NUUTEventBell* bell,
    bool trigger,
    float frequency)
{
    static constexpr float RATIOS[6] =
    {1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f};

    static constexpr float DECAY[6] =
        {0.30f, 0.25f, 0.20f, 0.16f, 0.12f, 0.09f};

    static constexpr float EXCITATION[6] =
        {0.35f, 0.40f, 0.38f, 0.32f, 0.25f, 0.18f};

    static constexpr float WEIGHT[6] =
        {0.08f, 0.30f, 0.48f, 0.62f, 0.70f, 0.65f};

    constexpr float TWO_PI = 6.28318530718f;
    const float sr = bell->sampleRate;

    if(trigger)
    {
        frequency = fmaxf(180.0f, fminf(700.0f, frequency));

        for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
        {
            float f = fminf(frequency * RATIOS[i], sr * 0.40f);
            float radius = expf(-1.0f / (DECAY[i] * sr));
            float omega = TWO_PI * f / sr;

            bell->coeffA[i] = 2.0f * radius * cosf(omega);
            bell->coeffB[i] = -(radius * radius);
        }
    }

    float output = 0.0f;

    for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
    {
        // A single excitation impulse starts the bell ringing.
        float excitation = trigger ? EXCITATION[i] : 0.0f;

        float y = excitation
                + bell->coeffA[i] * bell->y1[i]
                + bell->coeffB[i] * bell->y2[i];

        bell->y2[i] = bell->y1[i];
        bell->y1[i] = y;

        output += y * WEIGHT[i];
    }

    return output;
}

// ============================================================
// NUUT EVENT GONG
// ============================================================

struct NUUTEventGong
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};

    float sampleRate = 48000.0f;
};


inline void InitNUUTEventGong(
    NUUTEventGong* gong,
    float sampleRate)
{
    gong->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t i = 0;
        i < NUUTEventGong::MODE_COUNT;
        ++i)
    {
        gong->y1[i] = 0.0f;
        gong->y2[i] = 0.0f;

        gong->coeffA[i] = 0.0f;
        gong->coeffB[i] = 0.0f;
    }
}


inline float ProcessNUUTEventGong(
    NUUTEventGong* gong,
    bool trigger,
    float frequency)
{
    static constexpr float RATIOS[6] =
        {1.0f, 1.42f, 2.18f, 3.36f, 4.72f, 6.15f};

    static constexpr float DECAY[6] =
        {1.20f, 1.00f, 0.85f, 0.70f, 0.55f, 0.40f};

    static constexpr float EXCITATION[6] =
        {0.35f, 0.40f, 0.38f, 0.32f, 0.25f, 0.18f};

    static constexpr float WEIGHT[6] =
        {0.08f, 0.30f, 0.48f, 0.62f, 0.70f, 0.65f};

    constexpr float TWO_PI = 6.28318530718f;
    const float sr = gong->sampleRate;

    if(trigger)
    {
        frequency = fmaxf(
            180.0f,
            fminf(700.0f, frequency));

        for(std::size_t i = 0;
            i < NUUTEventGong::MODE_COUNT;
            ++i)
        {
            const float f = fminf(
                frequency * RATIOS[i],
                sr * 0.40f);

            const float radius =
                expf(-1.0f / (DECAY[i] * sr));

            const float omega = TWO_PI * f / sr;

            gong->coeffA[i] =
                2.0f * radius * cosf(omega);

            gong->coeffB[i] =
                -(radius * radius);
        }
    }

    float output = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventGong::MODE_COUNT;
        ++i)
    {
        const float excitation =
            trigger ? EXCITATION[i] : 0.0f;

        const float y =
            excitation
            + gong->coeffA[i] * gong->y1[i]
            + gong->coeffB[i] * gong->y2[i];

        gong->y2[i] = gong->y1[i];
        gong->y1[i] = y;

        output += y * WEIGHT[i];
    }

    return output;
}


// ============================================================
// NUUT EVENT TRIBAL DRUM
// A short, punchy modal percussion resonator.
// ============================================================

struct NUUTEventTribalDrum
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};

    float sampleRate = 48000.0f;
};


inline void InitNUUTEventTribalDrum(
    NUUTEventTribalDrum* drum,
    float sampleRate)
{
    drum->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t i = 0;
        i < NUUTEventTribalDrum::MODE_COUNT;
        ++i)
    {
        drum->y1[i] = 0.0f;
        drum->y2[i] = 0.0f;

        drum->coeffA[i] = 0.0f;
        drum->coeffB[i] = 0.0f;
    }
}


inline float ProcessNUUTEventTribalDrum(
    NUUTEventTribalDrum* drum,
    bool trigger,
    float frequency)
{
    static constexpr float RATIOS[6] =
        {1.0f, 1.31f, 1.77f, 2.43f, 3.18f, 4.27f};

    static constexpr float DECAY[6] =
        {0.18f, 0.14f, 0.11f, 0.085f, 0.065f, 0.045f};

    static constexpr float EXCITATION[6] =
        {0.70f, 0.55f, 0.42f, 0.30f, 0.22f, 0.15f};

    static constexpr float WEIGHT[6] =
        {0.70f, 0.62f, 0.48f, 0.34f, 0.24f, 0.16f};

    constexpr float TWO_PI = 6.28318530718f;
    const float sr = drum->sampleRate;

    if(trigger)
    {
        frequency = fmaxf(
            180.0f,
            fminf(700.0f, frequency));

        for(std::size_t i = 0;
            i < NUUTEventTribalDrum::MODE_COUNT;
            ++i)
        {
            const float f = fminf(
                frequency * RATIOS[i],
                sr * 0.40f);

            const float radius =
                expf(-1.0f / (DECAY[i] * sr));

            const float omega = TWO_PI * f / sr;

            drum->coeffA[i] =
                2.0f * radius * cosf(omega);

            drum->coeffB[i] =
                -(radius * radius);
        }
    }

    float output = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventTribalDrum::MODE_COUNT;
        ++i)
    {
        const float excitation =
            trigger ? EXCITATION[i] : 0.0f;

        const float y =
            excitation
            + drum->coeffA[i] * drum->y1[i]
            + drum->coeffB[i] * drum->y2[i];

        drum->y2[i] = drum->y1[i];
        drum->y1[i] = y;

        output += y * WEIGHT[i];
    }

    return output;
}

// ============================================================
// NUUT EVENT FLUTE
// A soft, harmonic, flute-like resonator voice.
// ============================================================

struct NUUTEventFlute
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};

    float sampleRate = 48000.0f;
};


// ------------------------------------------------------------
// INITIALIZE
// ------------------------------------------------------------

inline void InitNUUTEventFlute(
    NUUTEventFlute* flute,
    float sampleRate)
{
    flute->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t i = 0;
        i < NUUTEventFlute::MODE_COUNT;
        ++i)
    {
        flute->y1[i] = 0.0f;
        flute->y2[i] = 0.0f;

        flute->coeffA[i] = 0.0f;
        flute->coeffB[i] = 0.0f;
    }
}


// ------------------------------------------------------------
// PROCESS
// ------------------------------------------------------------

inline float ProcessNUUTEventFlute(
    NUUTEventFlute* flute,
    bool trigger,
    float frequency)
{
    static constexpr float RATIOS[6] =
    {
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        5.0f,
        6.0f
    };

    static constexpr float DECAY[6] =
    {
        0.42f,
        0.38f,
        0.32f,
        0.27f,
        0.22f,
        0.18f
    };

    static constexpr float EXCITATION[6] =
    {
        0.30f,
        0.24f,
        0.18f,
        0.13f,
        0.09f,
        0.06f
    };

    static constexpr float WEIGHT[6] =
    {
        0.90f,
        0.72f,
        0.48f,
        0.30f,
        0.18f,
        0.10f
    };

    constexpr float TWO_PI = 6.28318530718f;

    const float sr = flute->sampleRate;

    if(trigger)
    {
        frequency = fmaxf(
            180.0f,
            fminf(700.0f, frequency));

        for(std::size_t i = 0;
            i < NUUTEventFlute::MODE_COUNT;
            ++i)
        {
            const float f = fminf(
                frequency * RATIOS[i],
                sr * 0.40f);

            const float radius =
                expf(-1.0f / (DECAY[i] * sr));

            const float omega =
                TWO_PI * f / sr;

            flute->coeffA[i] =
                2.0f * radius * cosf(omega);

            flute->coeffB[i] =
                -(radius * radius);
        }
    }

    float output = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventFlute::MODE_COUNT;
        ++i)
    {
        // A soft impulse excites the resonant modes.
        const float excitation =
            trigger ? EXCITATION[i] : 0.0f;

        const float y =
            excitation
            + flute->coeffA[i] * flute->y1[i]
            + flute->coeffB[i] * flute->y2[i];

        flute->y2[i] = flute->y1[i];
        flute->y1[i] = y;

        output += y * WEIGHT[i];
    }

    return output;
}

// STRING

struct NUUTEventString
{
    static constexpr std::size_t MODE_COUNT = 6;
    static constexpr std::size_t NOTE_COUNT = 3;

    struct Resonator
    {
        float y1[MODE_COUNT] = {};
        float y2[MODE_COUNT] = {};
        float coeffA[MODE_COUNT] = {};
        float coeffB[MODE_COUNT] = {};
    };

    Resonator notes[NOTE_COUNT];

    float sampleRate = 48000.0f;
};

inline void InitNUUTEventString(
    NUUTEventString* s,
    float sampleRate)
{
    s->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t n = 0;
        n < NUUTEventString::NOTE_COUNT;
        ++n)
    {
        for(std::size_t i = 0;
            i < NUUTEventString::MODE_COUNT;
            ++i)
        {
            s->notes[n].y1[i] = 0.0f;
            s->notes[n].y2[i] = 0.0f;
            s->notes[n].coeffA[i] = 0.0f;
            s->notes[n].coeffB[i] = 0.0f;
        }
    }
}

inline float ProcessNUUTEventString(
    NUUTEventString* s,
    bool trigger,
    float frequency,
    int noteIndex)
{
    static constexpr float RATIOS[6] =
    {
        1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f
    };

    static constexpr float DECAY[6] =
    {
        1.20f, 0.90f, 0.65f, 0.45f, 0.32f, 0.22f
    };

    static constexpr float EXCITATION[6] =
    {
        0.32f, 0.24f, 0.17f, 0.12f, 0.08f, 0.05f
    };

    static constexpr float WEIGHT[6] =
    {
        0.85f, 0.70f, 0.52f, 0.36f, 0.23f, 0.14f
    };

    const float sr = s->sampleRate;
    float output = 0.0f;

    // Configure only the note being triggered.
    if(trigger &&
       noteIndex >= 0 &&
       noteIndex < static_cast<int>(
           NUUTEventString::NOTE_COUNT))
    {
        frequency = fminf(
            fmaxf(frequency, 180.0f),
            700.0f);

        auto& resonator = s->notes[noteIndex];

        for(std::size_t i = 0;
            i < NUUTEventString::MODE_COUNT;
            ++i)
        {
            const float f = fminf(
                frequency * RATIOS[i],
                sr * 0.40f);

            const float radius =
                expf(-1.0f / (DECAY[i] * sr));

            const float omega =
                6.28318530718f * f / sr;

            resonator.coeffA[i] =
                2.0f * radius * cosf(omega);

            resonator.coeffB[i] =
                -(radius * radius);
        }
    }

    // Process all notes that have been triggered.
    for(std::size_t n = 0;
        n < NUUTEventString::NOTE_COUNT;
        ++n)
    {
        auto& resonator = s->notes[n];

        // Excitation is applied only to the newly triggered note.
        const bool exciteThisNote =
            trigger && noteIndex == static_cast<int>(n);

        for(std::size_t i = 0;
            i < NUUTEventString::MODE_COUNT;
            ++i)
        {
            const float excitation =
                exciteThisNote ? EXCITATION[i] : 0.0f;

            const float y =
                excitation
                + resonator.coeffA[i] * resonator.y1[i]
                + resonator.coeffB[i] * resonator.y2[i];

            resonator.y2[i] = resonator.y1[i];
            resonator.y1[i] = y;

            output += y * WEIGHT[i];
        }
    }

    return output;
}


// ============================================================
// NUUT EVENT VOICE SELECTOR
// Selects exactly one event timbre at a time.
// ============================================================


enum class NUUTEventTimbre
{
    CrystalBell,
    Gong,
    TribalDrum,
    Flute,
    String
};


struct NUUTEventVoice
{
    NUUTEventBell bell;
    NUUTEventGong gong;
    NUUTEventTribalDrum tribalDrum;
    NUUTEventFlute flute;
    NUUTEventString string;

    NUUTEventTimbre timbre = NUUTEventTimbre::CrystalBell;

    float sampleRate = 48000.0f;
};


inline void InitNUUTEventVoice(
    NUUTEventVoice* voice,
    float sampleRate)
{
    voice->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    voice->timbre = NUUTEventTimbre::CrystalBell;

    InitNUUTEventBell(
        &voice->bell,
        voice->sampleRate);

    InitNUUTEventGong(
        &voice->gong,
        voice->sampleRate);

    InitNUUTEventTribalDrum(
        &voice->tribalDrum,
        voice->sampleRate);

    InitNUUTEventFlute(
        &voice->flute,
        voice->sampleRate);

    InitNUUTEventString(
        &voice->string,
        voice->sampleRate);
}


inline void SetNUUTEventTimbre(
    NUUTEventVoice* voice,
    NUUTEventTimbre timbre)
{
    voice->timbre = timbre;
}


inline float ProcessNUUTEventVoice(
    NUUTEventVoice* voice,
    bool trigger,
    float frequency,
    int stringNoteIndex = -1)
{
    switch(voice->timbre)
    {
        case NUUTEventTimbre::CrystalBell:
            return ProcessNUUTEventBell(
                &voice->bell,
                trigger,
                frequency);

        case NUUTEventTimbre::Gong:
            return ProcessNUUTEventGong(
                &voice->gong,
                trigger,
                frequency);

        case NUUTEventTimbre::TribalDrum:
            return ProcessNUUTEventTribalDrum(
                &voice->tribalDrum,
                trigger,
                frequency);

        case NUUTEventTimbre::Flute:
            return ProcessNUUTEventFlute(
                &voice->flute,
                trigger,
                frequency);

        case NUUTEventTimbre::String:
            return ProcessNUUTEventString(
                &voice->string,
                trigger,
                frequency,
                stringNoteIndex);

        default:
            return 0.0f;
    }
}