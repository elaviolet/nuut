#pragma once

#include "aspects/aspects_data.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

// ============================================================
// NUUT EVENTS - everything about the aspect events in one file
//
//   1. the five instruments (bell, gong, tribal drum, flute, strings)
//   2. the selector that plays one of them (NUUTEventVoice)
//   3. brightness modulation of the events
//   4. the aspect table: instrument, chord, volume and brightness
//      modulation of each aspect (search for "ASPECT PROFILES")
//
// The resonator of the planets (the modal resonator of the voices)
// stays in resonator.h.
// ============================================================


// Moves a frequency by octaves into [lo, hi] (hi must be >= 2 * lo).
inline float NUUTEventFoldFrequency(float f, float lo, float hi)
{
    if(f <= 0.0f)
        return lo;

    while(f < lo)
        f *= 2.0f;

    while(f > hi)
        f *= 0.5f;

    return f;
}


// ============================================================
// NUUT EVENT BELL
// A short, fluid melody played on bells. One trigger plays 5-7 notes
// that move step by step (like a little tune, not a chord): the melody
// starts and ends on notes of the chord, and in between it walks through
// a bell scale built on the chord, up and down over two octaves, with a
// gentle swell in strength. The bells ring into each other, so it flows.
// (Without a chord set, only the single triggering note is played.)
// ============================================================

struct NUUTEventBell
{
    static constexpr std::size_t MODE_COUNT = 6;
    static constexpr std::size_t BANK_COUNT = 8;   // one bell per note

    float y1[BANK_COUNT][MODE_COUNT] = {};
    float y2[BANK_COUNT][MODE_COUNT] = {};
    float coeffA[BANK_COUNT][MODE_COUNT] = {};
    float coeffB[BANK_COUNT][MODE_COUNT] = {};
    bool modeOn[BANK_COUNT][MODE_COUNT] = {};

    // The notes of the current melody.
    std::size_t noteCount = 0;
    std::size_t noteTime[BANK_COUNT] = {};
    float noteFreq[BANK_COUNT] = {};
    float noteVel[BANK_COUNT] = {};
    bool noteDone[BANK_COUNT] = {};
    std::size_t clock = 0;

    std::uint32_t rng = 13579u;

    float sampleRate = 48000.0f;
};

inline void StopNUUTEventBell(NUUTEventBell* bell)
{
    bell->noteCount = 0;
    bell->clock = 0;

    for(std::size_t n = 0; n < NUUTEventBell::BANK_COUNT; ++n)
    {
        for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
        {
            bell->y1[n][i] = 0.0f;
            bell->y2[n][i] = 0.0f;
        }
    }
}

inline void InitNUUTEventBell(
    NUUTEventBell* bell,
    float sampleRate)
{
    bell->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    for(std::size_t n = 0; n < NUUTEventBell::BANK_COUNT; ++n)
    {
        for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
        {
            bell->coeffA[n][i] = 0.0f;
            bell->coeffB[n][i] = 0.0f;
            bell->modeOn[n][i] = false;
        }
    }

    StopNUUTEventBell(bell);
}

// chord: the three notes of the chord, or nullptr for a single note.
inline float ProcessNUUTEventBell(
    NUUTEventBell* bell,
    bool trigger,
    float frequency,
    const float* chord = nullptr)
{
    static constexpr float RATIOS[6] =
    {1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f};

    static constexpr float DECAY[6] =
        {0.45f, 0.35f, 0.28f, 0.22f, 0.16f, 0.12f};

    static constexpr float EXCITATION[6] =
        {0.35f, 0.40f, 0.38f, 0.32f, 0.25f, 0.18f};

    static constexpr float WEIGHT[6] =
        {0.08f, 0.30f, 0.48f, 0.62f, 0.70f, 0.65f};

    constexpr float BELL_LEVEL = 2.6f;

    // Register of the bells: the scale starts anywhere between
    // REGISTER_LOW_HZ and REGISTER_LOW_HZ * 2^REGISTER_OCTAVES, then the
    // first note is placed one octave above (so about twice that).
    // Raise REGISTER_LOW_HZ to get higher bells.
    constexpr float REGISTER_LOW_HZ = 250.0f;
    constexpr float REGISTER_OCTAVES = 1.6f;
    constexpr float TWO_PI = 6.28318530718f;
    const float sr = bell->sampleRate;

    if(trigger)
    {
        auto rand01 = [bell]() {
            bell->rng = bell->rng * 1664525u + 1013904223u;
            return static_cast<float>(bell->rng >> 8) / 16777216.0f;
        };

        std::size_t count = 1;

        if(chord == nullptr)
        {
            bell->noteFreq[0] = fmaxf(180.0f, fminf(700.0f, frequency));
            bell->noteVel[0] = 1.0f;
            bell->noteTime[0] = 0;
        }
        else
        {
            // Bell scale: a major-ish scale plus the notes of the chord.
            // Every shower picks its own register: sometimes low bells,
            // sometimes high ones. The scale starts anywhere from about
            // 250 Hz (low) to about 1500 Hz (high).
            const float lowEdge =
                REGISTER_LOW_HZ * exp2f(REGISTER_OCTAVES * rand01());
            const float root =
                NUUTEventFoldFrequency(chord[0], lowEdge, 2.0f * lowEdge);

            bool inScale[12] = {};
            bool inChord[12] = {};
            static constexpr int BASE[6] = {0, 2, 4, 5, 7, 9};

            for(int b : BASE)
                inScale[b] = true;

            for(int c = 0; c < 3; ++c)
            {
                int semi = static_cast<int>(
                    roundf(12.0f * log2f(chord[c] / chord[0])));
                semi = ((semi % 12) + 12) % 12;
                inScale[semi] = true;
                inChord[semi] = true;
            }

            // Two octaves of scale steps.
            int steps[24];
            bool stepIsChord[24];
            int stepCount = 0;

            for(int oct = 0; oct < 2; ++oct)
            {
                for(int semi = 0; semi < 12; ++semi)
                {
                    if(inScale[semi] && semi + 12 * oct <= 14)
                    {
                        steps[stepCount] = semi + 12 * oct;
                        stepIsChord[stepCount] = inChord[semi];
                        ++stepCount;
                    }
                }
            }

            // Start on a chord note of the lower octave.
            int idx = 0;
            {
                int firstOct = 0;
                int candidates[12];

                for(int k = 0; k < stepCount && steps[k] < 12; ++k)
                    if(stepIsChord[k])
                        candidates[firstOct++] = k;

                idx = candidates[static_cast<int>(rand01() * firstOct) % firstOct];
            }

            count = 5 + static_cast<std::size_t>(rand01() * 3.0f) % 3; // 5-7

            int dir = 1;
            float t = 0.0f;

            for(std::size_t k = 0; k < count; ++k)
            {
                // The last note settles on a chord note.
                if(k == count - 1)
                {
                    int best = idx;
                    int bestDist = 1000;

                    for(int j = 0; j < stepCount; ++j)
                    {
                        const int dist = abs(j - idx);

                        if(stepIsChord[j] && dist < bestDist && j != idx)
                        {
                            best = j;
                            bestDist = dist;
                        }
                    }

                    idx = best;
                }

                const float semi = static_cast<float>(steps[idx]);
                bell->noteFreq[k] = root * exp2f(semi / 12.0f);

                // Strength rises and falls over the phrase.
                const float arc = sinf(3.14159265f
                    * (static_cast<float>(k) + 0.5f)
                    / static_cast<float>(count));
                bell->noteVel[k] = 0.45f + 0.45f * arc + 0.10f * rand01();

                bell->noteTime[k] = static_cast<std::size_t>(t * sr);

                // Flowing, slightly uneven pace; the last note is let ring.
                t += 0.17f + 0.13f * rand01();

                // Walk: mostly by one step, sometimes two; turn around
                // now and then and at the edges.
                if(rand01() < 0.25f)
                    dir = -dir;

                const int stride = rand01() < 0.7f ? 1 : 2;
                int next = idx + dir * stride;

                if(next < 0 || next >= stepCount)
                {
                    dir = -dir;
                    next = idx + dir * stride;
                }

                if(next < 0)
                    next = 0;
                if(next >= stepCount)
                    next = stepCount - 1;

                idx = next;
            }
        }

        for(std::size_t k = 0; k < count; ++k)
            bell->noteDone[k] = false;

        bell->noteCount = count;
        bell->clock = 0;
    }

    // Notes that are due now: tune one bell and strike it.
    float strike[NUUTEventBell::BANK_COUNT] = {};

    if(bell->noteCount > 0)
    {
        bool allDone = true;

        for(std::size_t k = 0; k < bell->noteCount; ++k)
        {
            if(bell->noteDone[k])
                continue;

            allDone = false;

            if(bell->clock >= bell->noteTime[k])
            {
                for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
                {
                    const float f = bell->noteFreq[k] * RATIOS[i];

                    // Partials that would be too high are left out.
                    bell->modeOn[k][i] = f < sr * 0.40f;

                    const float radius = expf(-1.0f / (DECAY[i] * sr));
                    const float omega = TWO_PI * fminf(f, sr * 0.40f) / sr;

                    bell->coeffA[k][i] = 2.0f * radius * cosf(omega);
                    bell->coeffB[k][i] = -(radius * radius);
                    bell->y1[k][i] = 0.0f;
                    bell->y2[k][i] = 0.0f;
                }

                strike[k] = bell->noteVel[k];
                bell->noteDone[k] = true;
            }
        }

        if(allDone)
            bell->noteCount = 0;
        else
            ++bell->clock;
    }

    float output = 0.0f;

    for(std::size_t n = 0; n < NUUTEventBell::BANK_COUNT; ++n)
    {
        for(std::size_t i = 0; i < NUUTEventBell::MODE_COUNT; ++i)
        {
            if(!bell->modeOn[n][i])
                continue;

            // A single excitation impulse starts the bell ringing.
            const float excitation = EXCITATION[i] * strike[n];

            const float y = excitation
                    + bell->coeffA[n][i] * bell->y1[n][i]
                    + bell->coeffB[n][i] * bell->y2[n][i];

            bell->y2[n][i] = bell->y1[n][i];
            bell->y1[n][i] = y;

            output += y * WEIGHT[i];
        }
    }

    return output * BELL_LEVEL;
}

// ============================================================
// NUUT EVENT GONG
// One deep, cavernous strike (a big tam-tam). The note is taken down
// into the low register, the first two partials are slightly detuned so
// the boom slowly "waves", and the middle and high partials bloom in
// after the hit, like the swell of a real gong. It is all pure tones:
// there is no noise in it. It rings for many seconds.
// ============================================================

struct NUUTEventGong
{
    static constexpr std::size_t MODE_COUNT = 8;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};

    // Each partial has a swell envelope (two one-pole smoothers): slow ones
    // make the upper partials swell after the strike.
    float smooth1[MODE_COUNT] = {};
    float smooth2[MODE_COUNT] = {};
    float smoothK[MODE_COUNT] = {};

    // sin(omega) of each partial: keeps the strike level independent of
    // the (very low) pitch.
    float coeffIn[MODE_COUNT] = {};

    // Each partial is also pushed by a pure tone at its own frequency,
    // which swells in after the strike (no noise: a clean bloom).
    float phC[MODE_COUNT] = {};
    float phS[MODE_COUNT] = {};
    float rotC[MODE_COUNT] = {};
    float rotS[MODE_COUNT] = {};

    // Deep sine body under the strike.
    float bodyPhase = 0.0f;
    float bodyFreq = 80.0f;
    float bodyEnv = 0.0f;
    float bodyDecay = 0.9999f;

    // Crash of noise at the strike.
    float crashEnv = 0.0f;
    float crashDecay = 0.999f;
    float crashLp = 0.0f;

    std::uint32_t rng = 31415u;

    float sampleRate = 48000.0f;
};


inline void StopNUUTEventGong(NUUTEventGong* gong)
{
    gong->bodyEnv = 0.0f;
    gong->crashEnv = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventGong::MODE_COUNT;
        ++i)
    {
        gong->y1[i] = 0.0f;
        gong->y2[i] = 0.0f;
        gong->smooth1[i] = 0.0f;
        gong->smooth2[i] = 0.0f;
    }
}


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
        gong->coeffA[i] = 0.0f;
        gong->coeffB[i] = 0.0f;
        gong->smoothK[i] = 1.0f;
    }

    StopNUUTEventGong(gong);
}


inline float ProcessNUUTEventGong(
    NUUTEventGong* gong,
    bool trigger,
    float frequency)
{
    // Inharmonic, like a metal plate. The first two are a detuned pair.
    static constexpr float RATIOS[8] =
        {1.000f, 1.006f, 1.52f, 2.07f, 2.76f, 3.62f, 4.74f, 6.10f};

    // Seconds: the low boom rings very long, the highs fade sooner.
    static constexpr float DECAY[8] =
        {8.0f, 7.0f, 6.0f, 5.5f, 4.5f, 3.8f, 3.0f, 2.2f};

    // Seconds for each partial to swell in (small = at the strike).
    static constexpr float BLOOM[8] =
        {0.004f, 0.006f, 0.02f, 0.06f, 0.12f, 0.20f, 0.30f, 0.40f};

    static constexpr float EXCITATION[8] =
        {0.26f, 0.24f, 0.26f, 0.26f, 0.26f, 0.24f, 0.22f, 0.20f};

    // Share of each partial that is struck at once (the rest blooms in).
    static constexpr float IMPACT[8] =
        {1.00f, 1.00f, 0.60f, 0.50f, 0.40f, 0.30f, 0.25f, 0.20f};

    constexpr float BLOOM_DRIVE = 8.0e-6f;

    static constexpr float WEIGHT[8] =
        {0.90f, 0.80f, 0.80f, 0.80f, 0.75f, 0.65f, 0.55f, 0.45f};

    // Overall level: a strike that really is heard.
    constexpr float GONG_LEVEL = 4.5f;
    constexpr float TWO_PI = 6.28318530718f;
    const float sr = gong->sampleRate;

    float strike = 0.0f;

    if(trigger)
    {
        // Cavernous: take the note down into 80 - 160 Hz.
        const float f0 = NUUTEventFoldFrequency(frequency, 80.0f, 160.0f);

        for(std::size_t i = 0;
            i < NUUTEventGong::MODE_COUNT;
            ++i)
        {
            const float f = fminf(f0 * RATIOS[i], sr * 0.40f);
            const float radius = expf(-1.0f / (DECAY[i] * sr));
            const float omega = TWO_PI * f / sr;

            gong->coeffA[i] = 2.0f * radius * cosf(omega);
            gong->coeffB[i] = -(radius * radius);
            gong->coeffIn[i] = sinf(omega);

            gong->phC[i] = 1.0f;
            gong->phS[i] = 0.0f;
            gong->rotC[i] = cosf(omega);
            gong->rotS[i] = sinf(omega);

            gong->smoothK[i] = 1.0f - expf(-1.0f / (BLOOM[i] * sr));
        }

        gong->bodyFreq = f0;
        gong->bodyEnv = 1.0f;
        gong->bodyDecay = expf(-1.0f / (1.6f * sr));

        strike = 1.0f;
    }

    float output = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventGong::MODE_COUNT;
        ++i)
    {
        // The strike goes through the smoothers: fast for the low
        // partials, slow (a swell) for the high ones.
        // Swell envelope of this partial (peak of about 1 after BLOOM).
        gong->smooth1[i] += gong->smoothK[i] * (strike - gong->smooth1[i]);
        gong->smooth2[i] += gong->smoothK[i] * (gong->smooth1[i] - gong->smooth2[i]);
        const float swell =
            gong->smooth2[i] / (0.368f * gong->smoothK[i]);

        // The pure tone of this partial (a rotating phasor, cheap).
        const float tone = gong->phS[i];
        const float nextC = gong->phC[i] * gong->rotC[i] - tone * gong->rotS[i];
        const float nextS = tone * gong->rotC[i] + gong->phC[i] * gong->rotS[i];
        gong->phC[i] = nextC;
        gong->phS[i] = nextS;

        // A hit right at the strike, plus the tone that swells in.
        const float y =
            strike * EXCITATION[i] * IMPACT[i] * gong->coeffIn[i]
            + tone * swell * BLOOM_DRIVE
            + gong->coeffA[i] * gong->y1[i]
            + gong->coeffB[i] * gong->y2[i];

        gong->y2[i] = gong->y1[i];
        gong->y1[i] = y;

        output += y * WEIGHT[i];
    }

    // Deep body.
    gong->bodyPhase += gong->bodyFreq / sr;
    if(gong->bodyPhase >= 1.0f)
        gong->bodyPhase -= 1.0f;

    const float body = sinf(TWO_PI * gong->bodyPhase) * gong->bodyEnv;
    gong->bodyEnv *= gong->bodyDecay;

    return (output * 1.5f + body * 0.9f) * GONG_LEVEL;
}


// ============================================================
// NUUT EVENT TRIBAL DRUM
// A hand-drum phrase (djembe / frame-drum style). One trigger plays a
// few deep strokes (2 or 3, a little different each time):
//   DUM : deep boom with a big pitch drop
//   KA  : middle tone, softer
//   TAK : high slap, short and sharp
// Each stroke is a pitch-dropping skin body, a noise slap and a modal
// skin with circular-membrane overtones. The drum is tuned low (the
// triggering note is folded down into 70-150 Hz).
// ============================================================

struct NUUTEventTribalDrum
{
    // 6 skin modes + 3 short metallic partials.
    static constexpr std::size_t MODE_COUNT = 9;
    static constexpr std::size_t PHRASE_STEPS = 3; // max strokes per trigger

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};
    float coeffIn[MODE_COUNT] = {};

    // Skin body: sine whose pitch drops right after the hit.
    float bodyPhase = 0.0f;
    float bodyBase = 100.0f;
    float bodyEnv = 0.0f;
    float bodyDecay = 0.999f;
    float pitchEnv = 0.0f;
    float pitchDrop = 1.0f;
    float pitchDecay = 0.99f;

    // Slap: bright noise burst.
    float slapEnv = 0.0f;
    float slapAmt = 0.5f;
    float slapDecay = 0.99f;
    float slapLp = 0.0f;

    // The modal skin is excited by one impulse per stroke.
    float impulse = 0.0f;

    std::uint32_t rng = 22222u;

    // Stroke sequencer: phraseStep >= phraseCount means idle.
    std::size_t phraseStep = 0;
    std::size_t phraseCount = 0;
    std::size_t samplesToNext = 0;
    std::size_t gapSamples[PHRASE_STEPS] = {};
    int strokeType[PHRASE_STEPS] = {};
    float strokeVel[PHRASE_STEPS] = {};
    float phraseBase = 100.0f;
    float rootBase = 100.0f;
    float strokePitch[PHRASE_STEPS] = {};

    float sampleRate = 48000.0f;
};


inline void StopNUUTEventTribalDrum(NUUTEventTribalDrum* drum)
{
    drum->phraseStep = 0;
    drum->phraseCount = 0;
    drum->samplesToNext = 0;
    drum->bodyEnv = 0.0f;
    drum->slapEnv = 0.0f;
    drum->impulse = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventTribalDrum::MODE_COUNT;
        ++i)
    {
        drum->y1[i] = 0.0f;
        drum->y2[i] = 0.0f;
    }
}


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
        drum->coeffA[i] = 0.0f;
        drum->coeffB[i] = 0.0f;
        drum->coeffIn[i] = 0.0f;
    }

    drum->bodyPhase = 0.0f;
    drum->pitchEnv = 0.0f;
    drum->slapLp = 0.0f;
    drum->pitchDecay = expf(-1.0f / (0.045f * drum->sampleRate));

    StopNUUTEventTribalDrum(drum);
}


// One stroke: type 0 = DUM, 1 = KA, 2 = TAK.
inline void NUUTEventTribalDrumStroke(
    NUUTEventTribalDrum* d,
    int type,
    float velocity)
{
    // The first six are the skin; the last three are a little metallic
    // ring (inharmonic, quick to fade).
    static constexpr float RATIOS[9] =
        {1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.90f, 5.60f, 7.70f};

    static constexpr float WEIGHT[9] =
        {0.45f, 0.30f, 0.25f, 0.20f, 0.14f, 0.10f, 0.26f, 0.18f, 0.10f};

    static constexpr float SKIN_TAU[9] =
        {1.00f, 0.75f, 0.70f, 0.55f, 0.45f, 0.40f, 1.30f, 1.00f, 0.80f};

    //                                    DUM    KA     TAK
    static constexpr float PITCH_MUL[3] = {1.00f, 1.50f, 2.00f};
    static constexpr float BODY_TAU[3]  = {0.15f, 0.18f, 0.10f};
    static constexpr float SKIN_BASE[3] = {0.10f, 0.15f, 0.09f};
    static constexpr float DROP[3]      = {1.30f, 0.80f, 0.40f};
    static constexpr float SLAP[3]      = {0.08f, 0.60f, 1.00f};
    static constexpr float SLAP_TAU[3]  = {0.010f, 0.008f, 0.006f};
    static constexpr float LEVEL[3]     = {1.20f, 0.90f, 0.90f};

    constexpr float TWO_PI = 6.28318530718f;

    if(type < 0)
        type = 0;
    if(type > 2)
        type = 2;

    const float sr = d->sampleRate;
    const float f0 = d->phraseBase * PITCH_MUL[type];

    for(std::size_t i = 0;
        i < NUUTEventTribalDrum::MODE_COUNT;
        ++i)
    {
        const float f = fminf(f0 * RATIOS[i], sr * 0.40f);
        const float radius = expf(-1.0f / (SKIN_BASE[type] * SKIN_TAU[i] * sr));
        const float omega = TWO_PI * f / sr;

        d->coeffA[i] = 2.0f * radius * cosf(omega);
        d->coeffB[i] = -(radius * radius);

        // sin(omega) keeps the stroke level independent of the pitch.
        d->coeffIn[i] = WEIGHT[i] * sinf(omega) * velocity * LEVEL[type];
    }

    d->bodyBase = f0;
    d->bodyEnv = velocity * LEVEL[type];
    d->bodyDecay = expf(-1.0f / (BODY_TAU[type] * sr));
    d->pitchEnv = 1.0f;
    d->pitchDrop = DROP[type];

    d->slapEnv = velocity;
    d->slapAmt = SLAP[type];
    d->slapDecay = expf(-1.0f / (SLAP_TAU[type] * sr));

    d->impulse = 1.0f;
}


inline float ProcessNUUTEventTribalDrum(
    NUUTEventTribalDrum* drum,
    bool trigger,
    float frequency)
{
    // Overall level, chosen to sit where the old drum did.
    constexpr float DRUM_LEVEL = 10.0f;
    constexpr float TWO_PI = 6.28318530718f;

    const float sr = drum->sampleRate;

    if(trigger)
    {
        // A new phrase cuts what is still ringing: strokes never pile up.
        StopNUUTEventTribalDrum(drum);

        drum->phraseBase =
            NUUTEventFoldFrequency(frequency, 110.0f, 220.0f);

        drum->rootBase = drum->phraseBase;

        auto rand01 = [drum]() {
            drum->rng = drum->rng * 1664525u + 1013904223u;
            return static_cast<float>(drum->rng >> 8) / 16777216.0f;
        };

        // "Tum tum" or at most "tum tum tum": a few deep, short strokes of
        // the same drum, close together, each a little different.
        drum->phraseCount = rand01() < 0.80f ? 2 : 3;
        drum->phraseStep = 0;
        drum->samplesToNext = 0;

        for(std::size_t s = 0; s < drum->phraseCount; ++s)
        {
            float gap = 0.0f;
            float vel = 1.0f;

            if(s > 0)
            {
                gap = 0.30f + 0.25f * rand01();
                vel = 0.55f + 0.30f * rand01();
            }

            drum->strokeType[s] = 0;
            drum->strokeVel[s] = vel;
            drum->strokePitch[s] = s == 0 ? 1.0f : 0.90f + 0.20f * rand01();
            drum->gapSamples[s] = static_cast<std::size_t>(gap * sr);
        }
    }

    // Stroke sequencer
    if(drum->phraseStep < drum->phraseCount)
    {
        if(drum->samplesToNext == 0)
        {
            const std::size_t s = drum->phraseStep;

            drum->phraseBase = drum->rootBase * drum->strokePitch[s];

            NUUTEventTribalDrumStroke(
                drum, drum->strokeType[s], drum->strokeVel[s]);

            ++drum->phraseStep;

            if(drum->phraseStep < drum->phraseCount)
                drum->samplesToNext = drum->gapSamples[drum->phraseStep];
        }
        else
        {
            --drum->samplesToNext;
        }
    }

    // Body: sine with a fast pitch drop.
    const float f =
        drum->bodyBase * (1.0f + drum->pitchDrop * drum->pitchEnv);
    drum->pitchEnv *= drum->pitchDecay;

    drum->bodyPhase += f / sr;
    if(drum->bodyPhase >= 1.0f)
        drum->bodyPhase -= 1.0f;

    const float body = sinf(TWO_PI * drum->bodyPhase) * drum->bodyEnv;
    drum->bodyEnv *= drum->bodyDecay;

    // Slap: bright noise (noise minus its own low-pass).
    drum->rng = drum->rng * 1664525u + 1013904223u;
    const float noise =
        static_cast<float>(static_cast<std::int32_t>(drum->rng))
        * 4.6566129e-10f;

    drum->slapLp += 0.12f * (noise - drum->slapLp);
    const float slap =
        (noise - drum->slapLp) * drum->slapEnv * drum->slapAmt;
    drum->slapEnv *= drum->slapDecay;

    // Skin overtones
    const float x = drum->impulse;
    drum->impulse = 0.0f;

    float skin = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventTribalDrum::MODE_COUNT;
        ++i)
    {
        const float y =
            drum->coeffIn[i] * x
            + drum->coeffA[i] * drum->y1[i]
            + drum->coeffB[i] * drum->y2[i];

        drum->y2[i] = drum->y1[i];
        drum->y1[i] = y;

        skin += y;
    }

    return (body * 0.55f + slap * 2.0f + skin * 2.0f) * DRUM_LEVEL;
}

// ============================================================
// NUUT EVENT FLUTE
// A breathy, blown flute. One trigger starts a short "blow": breath
// noise drives the resonant modes, so the tone swells in like air
// across a tube, holds, and then rings out. It sounds an octave up,
// so it stands out from the chord pad that doubles the same notes, and
// it has a slow tremolo and a little direct air.
// ============================================================

struct NUUTEventFlute
{
    static constexpr std::size_t MODE_COUNT = 6;

    float y1[MODE_COUNT] = {};
    float y2[MODE_COUNT] = {};

    float coeffA[MODE_COUNT] = {};
    float coeffB[MODE_COUNT] = {};
    float coeffIn[MODE_COUNT] = {};

    // Blow: number of samples of breath still to come, and its length.
    std::size_t blowTotal = 0;
    std::size_t blowLeft = 0;

    float tremPhase = 0.0f;
    float airLp = 0.0f;
    int lastNote = -1;

    // Second note, for the trill: a second bank of modes, cross-faded.
    float y1b[MODE_COUNT] = {};
    float y2b[MODE_COUNT] = {};
    float coeffA2[MODE_COUNT] = {};
    float coeffB2[MODE_COUNT] = {};
    float coeffIn2[MODE_COUNT] = {};

    bool trill = false;
    float trillRate = 6.5f;
    float trillPhase = 0.0f;

    // Entry of the blow (a longer attack makes a softer entry).
    float blowAttack = 0.30f;
    float chiffAmt = 1.2f;

    std::uint32_t rng = 54321u;

    float sampleRate = 48000.0f;
};


inline void StopNUUTEventFlute(NUUTEventFlute* flute)
{
    flute->blowLeft = 0;
    flute->tremPhase = 0.0f;
    flute->airLp = 0.0f;
    flute->trill = false;
    flute->trillPhase = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventFlute::MODE_COUNT;
        ++i)
    {
        flute->y1[i] = 0.0f;
        flute->y2[i] = 0.0f;
        flute->y1b[i] = 0.0f;
        flute->y2b[i] = 0.0f;
    }
}


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
        flute->coeffA[i] = 0.0f;
        flute->coeffB[i] = 0.0f;
        flute->coeffIn[i] = 0.0f;
        flute->coeffA2[i] = 0.0f;
        flute->coeffB2[i] = 0.0f;
        flute->coeffIn2[i] = 0.0f;
    }

    flute->blowTotal = 0;

    StopNUUTEventFlute(flute);
}


// ------------------------------------------------------------
// PROCESS
// ------------------------------------------------------------

inline float ProcessNUUTEventFlute(
    NUUTEventFlute* flute,
    bool trigger,
    float frequency,
    const float* chord = nullptr,
    float originRatio = 1.0f)
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

    // How long each mode keeps ringing after the blow stops.
    static constexpr float DECAY[6] =
    {
        0.60f,
        0.45f,
        0.38f,
        0.30f,
        0.24f,
        0.20f
    };

    // Hollow tone: strong fundamental, odd harmonics ahead of even ones.
    static constexpr float WEIGHT[6] =
    {
        0.90f,
        0.40f,
        0.42f,
        0.14f,
        0.16f,
        0.06f
    };

    constexpr float TWO_PI = 6.28318530718f;

    // Blow shape (seconds) and levels.
    constexpr float BLOW_SECONDS = 1.80f;
    constexpr float BLOW_ATTACK = 0.50f;
    constexpr float BLOW_RELEASE = 0.90f;
    constexpr float CHIFF_SECONDS = 0.06f;
    constexpr float BLOW_GAIN = 0.05f;
    constexpr float AIR_GAIN = 0.5f;
    constexpr float FLUTE_LEVEL = 2.2f;

    const float sr = flute->sampleRate;

    if(trigger)
    {
        auto rand01 = [flute]() {
            flute->rng = flute->rng * 1664525u + 1013904223u;
            return static_cast<float>(flute->rng >> 8) / 16777216.0f;
        };

        // With a chord set, the flute picks one of its three notes at
        // random (never the same one twice in a row).
        if(chord != nullptr)
        {
            int n = 0;

            do
            {
                n = static_cast<int>(rand01() * 3.0f) % 3;
            } while(n == flute->lastNote);

            flute->lastNote = n;
            frequency = chord[n];
        }

        // An octave up: the pad plays the same notes lower down.
        // Folded by octaves, so each note keeps its own pitch class.
        frequency = NUUTEventFoldFrequency(frequency * 2.0f, 440.0f, 1100.0f);

        // Origin knob: the notes move down or up (up to an octave), after
        // the folding, so the octave shift is kept.
        frequency *= fmaxf(0.5f, fminf(2.0f, originRatio));

        // Each blow is either a single note with a soft entry (half of
        // the time) or a little trill between two close notes.
        flute->trill = rand01() < 0.5f;

        float partner = frequency;

        if(flute->trill)
        {
            // A tone (or sometimes a semitone) above, like a flute trill.
            const float step = rand01() < 0.7f ? 1.122462f : 1.059463f;
            partner = frequency * step;
            flute->trillRate = 5.5f + 3.0f * rand01();
            flute->blowAttack = BLOW_ATTACK;
            flute->chiffAmt = 0.6f;
        }
        else
        {
            flute->blowAttack = 0.90f; // slower, softer entry
            flute->chiffAmt = 0.25f;   // less "tongue" at the start
        }

        for(std::size_t i = 0;
            i < NUUTEventFlute::MODE_COUNT;
            ++i)
        {
            const float radius =
                expf(-1.0f / (DECAY[i] * sr));

            const float fa = fminf(frequency * RATIOS[i], sr * 0.40f);
            const float fb = fminf(partner * RATIOS[i], sr * 0.40f);
            const float oa = TWO_PI * fa / sr;
            const float ob = TWO_PI * fb / sr;

            flute->coeffA[i] = 2.0f * radius * cosf(oa);
            flute->coeffB[i] = -(radius * radius);

            // sin(omega) keeps the level independent of the pitch.
            flute->coeffIn[i] = WEIGHT[i] * sinf(oa);

            flute->coeffA2[i] = 2.0f * radius * cosf(ob);
            flute->coeffB2[i] = -(radius * radius);
            flute->coeffIn2[i] = WEIGHT[i] * sinf(ob);
        }

        flute->blowTotal =
            static_cast<std::size_t>(BLOW_SECONDS * sr);
        flute->blowLeft = flute->blowTotal;
        flute->tremPhase = 0.0f;
        flute->trillPhase = 0.0f;
    }

    // Breath noise
    flute->rng = flute->rng * 1664525u + 1013904223u;
    const float noise =
        static_cast<float>(static_cast<std::int32_t>(flute->rng))
        * 4.6566129e-10f;

    // Blow envelope: swell in, hold, fade out, plus a short chiff.
    float blow = 0.0f;
    float chiff = 0.0f;

    if(flute->blowLeft > 0)
    {
        const float t =
            static_cast<float>(flute->blowTotal - flute->blowLeft) / sr;
        const float tLeft =
            static_cast<float>(flute->blowLeft) / sr;

        blow = fminf(1.0f, t / flute->blowAttack);
        blow = fminf(blow, tLeft / BLOW_RELEASE);

        chiff = expf(-t / CHIFF_SECONDS);

        --flute->blowLeft;
    }

    const float drive = noise * (blow + flute->chiffAmt * chiff) * BLOW_GAIN;

    float modalOutput = 0.0f;

    for(std::size_t i = 0;
        i < NUUTEventFlute::MODE_COUNT;
        ++i)
    {
        const float y =
            flute->coeffIn[i] * drive
            + flute->coeffA[i] * flute->y1[i]
            + flute->coeffB[i] * flute->y2[i];

        flute->y2[i] = flute->y1[i];
        flute->y1[i] = y;

        modalOutput += y;
    }

    // Trill: second note, cross-faded in and out of the first one.
    if(flute->trill)
    {
        float modalB = 0.0f;

        for(std::size_t i = 0;
            i < NUUTEventFlute::MODE_COUNT;
            ++i)
        {
            const float y =
                flute->coeffIn2[i] * drive
                + flute->coeffA2[i] * flute->y1b[i]
                + flute->coeffB2[i] * flute->y2b[i];

            flute->y2b[i] = flute->y1b[i];
            flute->y1b[i] = y;

            modalB += y;
        }

        flute->trillPhase += flute->trillRate / sr;
        if(flute->trillPhase >= 1.0f)
            flute->trillPhase -= 1.0f;

        // The trill starts a little after the note, then goes on.
        const float tPlayed =
            static_cast<float>(flute->blowTotal - flute->blowLeft) / sr;
        const float fade = fmaxf(0.0f, fminf(1.0f, (tPlayed - 0.45f) / 0.35f));

        const float wB =
            fade * (0.5f - 0.5f * cosf(TWO_PI * flute->trillPhase));

        modalOutput = modalOutput * (1.0f - wB) + modalB * wB;
    }

    // A little direct air (noise minus its own low-pass).
    flute->airLp += 0.12f * (noise - flute->airLp);
    const float air = (noise - flute->airLp) * blow * AIR_GAIN;

    // Slow tremolo that grows after the first half second.
    flute->tremPhase += 5.0f / sr;
    if(flute->tremPhase >= 1.0f)
        flute->tremPhase -= 1.0f;

    const float tremFade =
        fminf(1.0f,
              static_cast<float>(flute->blowTotal - flute->blowLeft)
                  / (0.8f * sr));

    const float trem =
        1.0f + 0.15f * tremFade * sinf(TWO_PI * flute->tremPhase);

    return (modalOutput * trem + air) * FLUTE_LEVEL;
}

// ============================================================
// STRING
// Plucked strings in the style of an Indian string instrument
// (sitar / sarod / veena): a bright pluck, a long ringing sustain and a
// soft "buzz" in the upper partials, like a string touching its bridge.
// Each chord note is plucked on its own, one after the other, and the
// notes keep ringing together, so the chord flows like a strum.
// ============================================================

struct NUUTEventString
{
    static constexpr std::size_t MODE_COUNT = 10;
    static constexpr std::size_t NOTE_COUNT = 6; // strings that can ring together

    struct Resonator
    {
        float y1[MODE_COUNT] = {};
        float y2[MODE_COUNT] = {};
        float coeffA[MODE_COUNT] = {};
        float coeffB[MODE_COUNT] = {};
    };

    Resonator notes[NOTE_COUNT];

    // Strum: the three strings are plucked one right after the other.
    float strumFreq[NOTE_COUNT] = {};
    float strumVel[NOTE_COUNT] = {};
    std::size_t strumDelay[NOTE_COUNT] = {};
    bool strumPending[NOTE_COUNT] = {};

    std::uint32_t rng = 97531u;

    float sampleRate = 48000.0f;
};

inline void StopNUUTEventString(NUUTEventString* s)
{
    for(std::size_t n = 0; n < NUUTEventString::NOTE_COUNT; ++n)
    {
        s->strumPending[n] = false;

        for(std::size_t i = 0; i < NUUTEventString::MODE_COUNT; ++i)
        {
            s->notes[n].y1[i] = 0.0f;
            s->notes[n].y2[i] = 0.0f;
        }
    }
}

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
            s->notes[n].coeffA[i] = 0.0f;
            s->notes[n].coeffB[i] = 0.0f;
        }
    }

    StopNUUTEventString(s);
}

// chord: the three notes of the chord, or nullptr (then the single
// frequency is used for whichever note is triggered).
inline float ProcessNUUTEventString(
    NUUTEventString* s,
    bool trigger,
    float frequency,
    int noteIndex,
    const float* chord = nullptr)
{
    // Slightly stretched, like a real string.
    static constexpr float RATIOS[10] =
    {
        1.000f, 2.001f, 3.003f, 4.006f, 5.010f,
        6.015f, 7.021f, 8.028f, 9.036f, 10.045f
    };

    // Long sustain; the upper partials ring on (the buzz).
    static constexpr float DECAY[10] =
    {
        2.60f, 2.20f, 1.90f, 1.70f, 1.50f,
        1.30f, 1.10f, 0.95f, 0.80f, 0.70f
    };

    // Pluck brightness (before the pluck position is applied).
    static constexpr float EXCITATION[10] =
    {
        0.26f, 0.24f, 0.20f, 0.17f, 0.14f,
        0.12f, 0.10f, 0.08f, 0.07f, 0.06f
    };

    static constexpr float WEIGHT[10] =
    {
        0.85f, 0.70f, 0.58f, 0.55f, 0.52f,
        0.46f, 0.40f, 0.34f, 0.28f, 0.22f
    };

    // Partials from this one up go through the soft "buzz".
    constexpr std::size_t BUZZ_FROM = 3;
    constexpr float BUZZ_DRIVE = 2.5f;
    constexpr float STRING_LEVEL = 1.4f;
    constexpr float PI = 3.14159265359f;
    constexpr float TWO_PI = 6.28318530718f;

    const float sr = s->sampleRate;

    auto rand01 = [s]() {
        s->rng = s->rng * 1664525u + 1013904223u;
        std::uint32_t x = s->rng;
        x ^= x >> 16;
        x *= 0x45d9f3bu;
        x ^= x >> 16;
        return static_cast<float>(x >> 8) / 16777216.0f;
    };

    // What each string is plucked with in this sample (zero = nothing).
    float excite[NUUTEventString::NOTE_COUNT][NUUTEventString::MODE_COUNT] = {};

    // Tunes string n to f0 and fills its pluck.
    auto pluck = [&](std::size_t n, float f0, float vel) {
        // Each pluck is a little different: the pluck position moves.
        const float pos = 0.12f + 0.14f * rand01();

        auto& r = s->notes[n];

        for(std::size_t i = 0; i < NUUTEventString::MODE_COUNT; ++i)
        {
            const float f = fminf(f0 * RATIOS[i], sr * 0.40f);
            const float radius = expf(-1.0f / (DECAY[i] * sr));
            const float omega = TWO_PI * f / sr;

            r.coeffA[i] = 2.0f * radius * cosf(omega);
            r.coeffB[i] = -(radius * radius);

            const float k = static_cast<float>(i + 1);
            excite[n][i] =
                EXCITATION[i] * (0.35f + fabsf(sinf(PI * k * pos))) * vel;
        }
    };

    if(trigger)
    {
        if(chord != nullptr)
        {
            // One trigger plays one gesture on the chord. The gesture is
            // different every time, so the strings never sound the same:
            //   - a quick strum of the three notes (about a third of the time)
            //   - a slow, harp-like spread of the notes
            //   - a short flowing phrase of plucks, like the opening of a raga
            // (The later notes that the main asks for are not needed.)
            if(noteIndex == 0)
            {
                // The chord notes on a ladder of two octaves, lowest first.
                float ladder[6];
                for(std::size_t n = 0; n < 3; ++n)
                {
                    ladder[n] = NUUTEventFoldFrequency(chord[n], 200.0f, 400.0f);
                    ladder[n + 3] = ladder[n] * 2.0f;
                }
                for(int a = 0; a < 5; ++a)
                    for(int b = 0; b < 5 - a; ++b)
                        if(ladder[b] > ladder[b + 1])
                        {
                            const float t = ladder[b];
                            ladder[b] = ladder[b + 1];
                            ladder[b + 1] = t;
                        }

                const float kind = rand01();
                std::size_t count = 0;
                float t = 0.0f;

                if(kind < 0.35f)
                {
                    // Quick strum: the three notes, 50 - 120 ms apart, in a
                    // random voicing (some notes an octave higher).
                    count = 3;
                    const bool down = rand01() < 0.6f;

                    for(std::size_t k = 0; k < 3; ++k)
                    {
                        const std::size_t n = down ? k : 2 - k;
                        float f = ladder[n];            // the three lowest
                        if(rand01() < 0.30f)
                            f *= 2.0f;                  // some an octave up

                        s->strumFreq[k] = f;
                        s->strumVel[k] = k == 0 ? 1.0f : 0.75f + 0.20f * rand01();
                        s->strumDelay[k] = static_cast<std::size_t>(t * sr);
                        t += 0.05f + 0.07f * rand01();
                    }
                }
                else if(kind < 0.65f)
                {
                    // Harp-like spread: slow, one note after the other, up
                    // or down, 0.16 - 0.32 s apart.
                    count = 4;
                    const bool up = rand01() < 0.6f;

                    for(std::size_t k = 0; k < 4; ++k)
                    {
                        s->strumFreq[k] = ladder[up ? k : 3 - k];
                        s->strumVel[k] = 0.80f + 0.20f * rand01();
                        s->strumDelay[k] = static_cast<std::size_t>(t * sr);
                        t += 0.16f + 0.16f * rand01();
                    }
                }
                else
                {
                    // Short phrase: 5 - 6 plucks walking along the ladder.
                    count = 5 + static_cast<std::size_t>(rand01() * 2.0f) % 2;
                    int idx = static_cast<int>(rand01() * 3.0f) % 3;
                    int dir = 1;

                    for(std::size_t k = 0; k < count; ++k)
                    {
                        s->strumFreq[k] = ladder[idx];

                        const float arc = sinf(3.14159265f
                            * (static_cast<float>(k) + 0.5f)
                            / static_cast<float>(count));
                        s->strumVel[k] = 0.55f + 0.40f * arc + 0.05f * rand01();
                        s->strumDelay[k] = static_cast<std::size_t>(t * sr);
                        t += 0.15f + 0.17f * rand01();

                        if(rand01() < 0.25f)
                            dir = -dir;

                        int next = idx + dir * (rand01() < 0.7f ? 1 : 2);
                        if(next < 0 || next > 5)
                        {
                            dir = -dir;
                            next = idx + dir;
                        }
                        idx = next < 0 ? 0 : (next > 5 ? 5 : next);
                    }
                }

                for(std::size_t k = 0; k < NUUTEventString::NOTE_COUNT; ++k)
                    s->strumPending[k] = k < count;
            }
        }
        else if(noteIndex >= 0
                && noteIndex < static_cast<int>(3))
        {
            // Without a chord: one string per trigger, as before.
            pluck(static_cast<std::size_t>(noteIndex),
                  fmaxf(180.0f, fminf(700.0f, frequency)),
                  1.0f);
        }
    }

    // Strum plucks that are due now.
    for(std::size_t n = 0; n < NUUTEventString::NOTE_COUNT; ++n)
    {
        if(!s->strumPending[n])
            continue;

        if(s->strumDelay[n] == 0)
        {
            pluck(n, s->strumFreq[n], s->strumVel[n]);
            s->strumPending[n] = false;
        }
        else
        {
            --s->strumDelay[n];
        }
    }

    float output = 0.0f;

    for(std::size_t n = 0; n < NUUTEventString::NOTE_COUNT; ++n)
    {
        auto& r = s->notes[n];

        float low = 0.0f;
        float high = 0.0f;

        for(std::size_t i = 0; i < NUUTEventString::MODE_COUNT; ++i)
        {
            const float y =
                excite[n][i]
                + r.coeffA[i] * r.y1[i]
                + r.coeffB[i] * r.y2[i];

            r.y2[i] = r.y1[i];
            r.y1[i] = y;

            if(i < BUZZ_FROM)
                low += y * WEIGHT[i];
            else
                high += y * WEIGHT[i];
        }

        // Soft saturation of the upper partials: the jawari buzz.
        const float h = high * BUZZ_DRIVE;
        const float h2 = h * h;
        const float buzz = (fabsf(h) > 3.0f)
            ? (h > 0.0f ? 1.0f : -1.0f)
            : h * (27.0f + h2) / (27.0f + 9.0f * h2);

        output += low + buzz / BUZZ_DRIVE;
    }

    return output * STRING_LEVEL;
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

    // The three notes of the current chord (see SetNUUTEventChord).
    float chord[3] = {432.0f, 432.0f, 432.0f};
    bool hasChord = false;

    // Ratio of the Origin knob (0.5 = an octave down, 2 = an octave up).
    float originRatio = 1.0f;

    // Brightness modulation: a low-pass whose cutoff breathes slowly.
    // brightDepthOct = 0 switches it off (the sound passes untouched).
    float brightRateHz     = 0.0f;
    float brightDepthOct   = 0.0f;
    float brightCeilingHz  = 9000.0f;
    float brightPhase      = 0.0f;
    float brightK          = 1.0f;
    float brightState      = 0.0f;
    unsigned brightCounter = 0;

    float sampleRate = 48000.0f;
};


inline void InitNUUTEventVoice(
    NUUTEventVoice* voice,
    float sampleRate)
{
    voice->sampleRate =
        sampleRate > 0.0f ? sampleRate : 48000.0f;

    voice->timbre = NUUTEventTimbre::CrystalBell;
    voice->brightPhase   = 0.0f;
    voice->brightState   = 0.0f;
    voice->brightK       = 1.0f;
    voice->brightCounter = 0;

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


// Tells the events the three notes of the current chord. Bell, strings and
// flute then play these notes; without it they play the single frequency.
inline void SetNUUTEventChord(
    NUUTEventVoice* voice,
    float note0,
    float note1,
    float note2)
{
    voice->chord[0] = note0;
    voice->chord[1] = note1;
    voice->chord[2] = note2;
    voice->hasChord = true;
}


// Origin knob ratio (same value the main uses for the pads); the flute
// follows it.
inline void SetNUUTEventOrigin(
    NUUTEventVoice* voice,
    float originRatio)
{
    voice->originRatio = originRatio;
}


inline void SetNUUTEventTimbre(
    NUUTEventVoice* voice,
    NUUTEventTimbre timbre)
{
    if(timbre != voice->timbre)
    {
        StopNUUTEventBell(&voice->bell);
        StopNUUTEventGong(&voice->gong);
        StopNUUTEventTribalDrum(&voice->tribalDrum);
        StopNUUTEventFlute(&voice->flute);
        StopNUUTEventString(&voice->string);
    }

    voice->timbre = timbre;
}


inline float ProcessNUUTEventVoiceRaw(
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
                frequency,
                voice->hasChord ? voice->chord : nullptr);

        case NUUTEventTimbre::Gong:
            return ProcessNUUTEventGong(
                &voice->gong,
                trigger,
                voice->hasChord ? voice->chord[0] : frequency);

        case NUUTEventTimbre::TribalDrum:
            return ProcessNUUTEventTribalDrum(
                &voice->tribalDrum,
                trigger,
                voice->hasChord ? voice->chord[0] : frequency);

        case NUUTEventTimbre::Flute:
            return ProcessNUUTEventFlute(
                &voice->flute,
                trigger,
                frequency,
                voice->hasChord ? voice->chord : nullptr,
                voice->originRatio);

        case NUUTEventTimbre::String:
            return ProcessNUUTEventString(
                &voice->string,
                trigger,
                frequency,
                stringNoteIndex,
                voice->hasChord ? voice->chord : nullptr);

        default:
            return 0.0f;
    }
}


// Sets the brightness modulation of the events: how fast the cutoff
// breathes (Hz), how far it falls from the ceiling (in octaves) and the
// ceiling itself (Hz). Depth 0 = off. Call it once per block.
inline void SetNUUTEventBrightness(
    NUUTEventVoice* voice,
    float rateHz,
    float depthOctaves,
    float ceilingHz)
{
    voice->brightRateHz    = rateHz;
    voice->brightDepthOct  = depthOctaves;
    voice->brightCeilingHz = ceilingHz;
}


// Plays the selected event, then applies the brightness modulation.
inline float ProcessNUUTEventVoice(
    NUUTEventVoice* voice,
    bool trigger,
    float frequency,
    int stringNoteIndex = -1)
{
    const float x = ProcessNUUTEventVoiceRaw(
        voice, trigger, frequency, stringNoteIndex);

    if(voice->brightDepthOct <= 0.0f)
        return x;

    voice->brightPhase += voice->brightRateHz / voice->sampleRate;
    if(voice->brightPhase >= 1.0f)
        voice->brightPhase -= 1.0f;

    // The cutoff is updated every 32 samples (exp and pow are not free).
    if((++voice->brightCounter & 31u) == 0u)
    {
        // 0 at the bright end, 1 at the dark end of the breathing.
        const float u =
            0.5f + 0.5f * sinf(6.2831853f * voice->brightPhase);
        const float cutoff =
            voice->brightCeilingHz
            * exp2f(-voice->brightDepthOct * u);
        voice->brightK =
            1.0f - expf(-6.2831853f * cutoff / voice->sampleRate);
    }

    voice->brightState += voice->brightK * (x - voice->brightState);
    return voice->brightState;
}


// ============================================================
// ASPECT PROFILES
// Everything that makes one aspect sound different from another lives
// in this one table: which instrument plays, which chord it builds, how
// its volume breathes, how its brightness breathes, and whether the
// chord pad joins in.
//
// To tune an aspect, change its row. Nothing else needs to move.
// The rows are in the same order as AspectType:
//   0 Conjunction, 1 Sextile, 2 Square, 3 Trine, 4 Opposition
// ============================================================

// Semitone ratios used by the chords (equal temperament).
constexpr float kSemi4  = 1.259921f;
constexpr float kSemi5  = 1.334840f;
constexpr float kSemi7  = 1.498307f;
constexpr float kSemi9  = 1.681793f;
constexpr float kSemi10 = 1.781797f;

// Chord notes above the root (the root itself is the planet's note).
struct ChordRatios
{
    float second;
    float third;
};

struct AspectProfile
{
    const char*     name;
    NUUTEventTimbre timbre;        // instrument of the event
    ChordRatios     chord;         // second and third note of the chord
    float           modRateHz;     // speed of the slow volume breathing
    float           modDepth;      // how much it breathes (0.10 = +-10%)
    float           brightRateHz;  // speed of the brightness breathing
    float           brightDepthOct;// how far the cutoff falls, in octaves (0 = off)
    float           brightCeilingHz; // cutoff at the bright end
    bool            usesChordPad;  // extra voices 4-6 play the chord notes
    bool            singleNote;    // the event plays only the root note
};

constexpr AspectProfile kAspectProfiles[5] =
{
    //  name          instrument                    chord              vol rate/depth   bright rate/oct/ceiling   pad    single
    {"Conjunction", NUUTEventTimbre::CrystalBell, {kSemi5, kSemi9},  0.18f, 0.07f,   0.07f, 2.0f, 9000.0f,  false, false}, // 0, +5, +9
    {"Sextile",     NUUTEventTimbre::String,      {kSemi5, kSemi10}, 0.45f, 0.04f,   0.11f, 2.5f, 9000.0f,  false, false}, // 0, +5, +10
    {"Square",      NUUTEventTimbre::TribalDrum,  {kSemi4, kSemi9},  2.00f, 0.10f,   0.50f, 2.0f, 9000.0f,  false, false}, // 0, +4, +9
    {"Trine",       NUUTEventTimbre::Flute,       {kSemi4, kSemi7},  0.12f, 0.05f,   0.09f, 2.0f, 9000.0f,  false, false}, // 0, +4, +7
    {"Opposition",  NUUTEventTimbre::Gong,        {kSemi4, kSemi7},  0.65f, 0.08f,   0.05f, 2.5f, 9000.0f,  false, true }  // 0, +4, +7
};

// Returns the profile of an aspect, or nullptr when there is none.
inline const AspectProfile* GetAspectProfile(int aspect)
{
    if(aspect < 0 || aspect >= 5)
        return nullptr;
    return &kAspectProfiles[aspect];
}

inline const ChordRatios* ChordForAspect(int aspect)
{
    const AspectProfile* profile = GetAspectProfile(aspect);
    return profile != nullptr ? &profile->chord : nullptr;
}

// Aspects whose event also plays the chord pad. Set usesChordPad to true
// in the table above to turn it on for an aspect.
inline bool AspectUsesChordPad(int aspect)
{
    const AspectProfile* profile = GetAspectProfile(aspect);
    return profile != nullptr && profile->usesChordPad;
}