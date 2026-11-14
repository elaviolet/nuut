#pragma once

#include "daisysp.h"
#include "transit.h"
#include "resonator.h"

#include <cmath>
#include <cstring>
#include <cstdint>
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

    // Alchemy FM index (radians), smoothed per sample.
    float fmSmoothed = 0.0f;
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

// Small replacements for log2f / exp2f: calling those pulls extra library code
// into the flash, which is very tight. Accurate to far better than 1 cent.
inline float NuutLog2(float x)
{
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    int e = static_cast<int>((bits >> 23) & 255u) - 127;
    bits  = (bits & 0x007FFFFFu) | 0x3F800000u;   // mantissa in [1, 2)
    float m;
    std::memcpy(&m, &bits, sizeof(m));
    if(m > 1.41421356f)
    {
        m *= 0.5f;
        e += 1;
    }
    const float z  = (m - 1.0f) / (m + 1.0f);
    const float z2 = z * z;
    const float ln = 2.0f * z
                     * (1.0f + z2 * (1.0f / 3.0f + z2 * (1.0f / 5.0f + z2 * (1.0f / 7.0f))));
    return static_cast<float>(e) + ln * 1.44269504f;
}

inline float NuutExp2(float y)
{
    return expf(y * 0.69314718f);
}

// ============================================================
// KOSMOS - chords built on the root (voice 1)
//
// Kosmos picks one of nine chords. The first voice is the root and
// never moves. Every other voice keeps its own planet pitch, but is
// moved by the smallest number of semitones onto the nearest note of
// the chord, counted from the root. So each voice stays in its own
// register and the chord is always in tune.
// ============================================================

// Zone 0 is "Planets": Kosmos off, every voice plays its own Cousto note.
// Zones 1..9 are the nine chords below.
constexpr int kKosmosChords = 9;
constexpr int kKosmosZones  = kKosmosChords + 1;

// Pitch classes of each chord, as bits: bit n = n semitones above the root.
// Chords (zones 1..9): Power, Sus4, Sus2, Minor, Min7, Major, Maj7, Add9, Lydian
constexpr unsigned kKosmosChord[kKosmosChords] = {
    (1u << 0) | (1u << 7),                                          // Power:  0 7
    (1u << 0) | (1u << 5) | (1u << 7),                              // Sus4:   0 5 7
    (1u << 0) | (1u << 2) | (1u << 7),                              // Sus2:   0 2 7
    (1u << 0) | (1u << 3) | (1u << 7),                              // Minor:  0 3 7
    (1u << 0) | (1u << 3) | (1u << 7) | (1u << 10),                 // Min7:   0 3 7 10
    (1u << 0) | (1u << 4) | (1u << 7),                              // Major:  0 4 7
    (1u << 0) | (1u << 4) | (1u << 7) | (1u << 11),                 // Maj7:   0 4 7 11
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7),                  // Add9:   0 2 4 7
    (1u << 0) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 11),     // Lydian: 0 4 6 7 11 (maj7#11)
};

// Degrees played by voice 2 and voice 3 for each chord (semitones above the
// root, 0..11). Each voice takes its degree in the octave closest to its own
// planet pitch, so it keeps its register and the chord colour is always heard.
// Voice 1 is the root and never moves. Celestia voices fill in the rest.
constexpr int kKosmosDegree[kKosmosChords][2] = {
    {7, 0},    // Power:  fifth, octave
    {5, 7},    // Sus4:   fourth, fifth
    {2, 7},    // Sus2:   second, fifth
    {3, 7},    // Minor:  minor third, fifth
    {3, 10},   // Min7:   minor third, minor seventh
    {4, 7},    // Major:  major third, fifth
    {4, 11},   // Maj7:   major third, major seventh
    {4, 2},    // Add9:   major third, ninth
    {4, 6},    // Lydian: major third, sharp eleventh
};

// Ratio that moves noteHz onto the chord degree of `voice` (0 = voice 2,
// 1 = voice 3), in the octave closest to the original pitch.
inline float KosmosDegreeRatio(float rootHz, float noteHz, int zone, int voice)
{
    if(zone <= 0 || rootHz <= 1.0f || noteHz <= 1.0f)
        return 1.0f;   // zone 0: Kosmos off
    if(zone >= kKosmosZones)
        zone = kKosmosZones - 1;
    const int degree = kKosmosDegree[zone - 1][voice];
    const float s = 12.0f * NuutLog2(noteHz / rootHz);
    // Nearest value of the form degree + 12 * k.
    const float k = floorf((s - static_cast<float>(degree)) / 12.0f + 0.5f);
    const float m = static_cast<float>(degree) + 12.0f * k;
    return NuutExp2((m - s) / 12.0f);
}

// Frequency ratio that moves noteHz onto the nearest note of the chord
// built on rootHz. avoidPc (0..11, or -1) is a pitch class that must not
// be used, so two voices do not land on the same note. outPc receives the
// pitch class that was chosen.
inline float KosmosSnapRatio(
    float rootHz, float noteHz, int zone, int avoidPc, int* outPc)
{
    if(outPc)
        *outPc = -1;
    if(rootHz <= 1.0f || noteHz <= 1.0f)
        return 1.0f;

    if(zone <= 0)
        return 1.0f;   // zone 0: Kosmos off
    if(zone >= kKosmosZones)
        zone = kKosmosZones - 1;

    const unsigned mask = kKosmosChord[zone - 1];
    const float s = 12.0f * NuutLog2(noteHz / rootHz);   // semitones above the root
    const int   r = static_cast<int>(floorf(s + 0.5f));

    int   best     = 0;
    float bestDist = 1.0e9f;
    bool  found    = false;
    for(int d = -7; d <= 7; d++)
    {
        const int m  = r + d;
        const int pc = ((m % 12) + 12) % 12;
        if(!((mask >> pc) & 1u) || pc == avoidPc)
            continue;
        const float dist = fabsf(static_cast<float>(m) - s);
        if(dist < bestDist)
        {
            bestDist = dist;
            best     = m;
            found    = true;
        }
    }
    if(!found)
        return 1.0f;

    if(outPc)
        *outPc = ((best % 12) + 12) % 12;
    return NuutExp2((static_cast<float>(best) - s) / 12.0f);
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
    float waveMorph = 0.0f,
    float brightness = 0.0f)   // 0 = fader up, 1 = fader all the way down
{
    // Silent voice: nothing audible, skip all DSP.
    if(!gate && voice->envelope <= 0.0f)
        return 0.0f;

    // KOSMOS: the parameter `kosmos` is now the frequency ratio that puts this
    // voice on its chord note (computed once per block in the main file).
    (void)voiceIndex;
    const float harmonicRatio = kosmos;

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

    // FM (brightness): a sine modulator at exactly four times the frequency
    // (4:1, so it stays harmonic and in tune) bends the phase of the sine. It
    // comes in only when the Alchemy fader goes DOWN (index = brightness
    // squared, up to kAlchemyFmMax) and is smoothed per sample, so there are no
    // clicks. At 130-450 Hz the strong sidebands stay below about 8 kHz, so
    // aliasing is not an issue.
    constexpr float kAlchemyFmMax   = 0.45f;
    constexpr float kAlchemyFmRatio = 4.0f;
    voice->fmSmoothed
        += 0.002f * (kAlchemyFmMax * brightness * brightness - voice->fmSmoothed);

    float wavePh = ph;
    if(voice->fmSmoothed > 0.002f)
    {
        float ph2 = ph * kAlchemyFmRatio;
        ph2 -= floorf(ph2);
        wavePh = ph + voice->fmSmoothed * (1.0f / 6.2831853f) * FastSin01(ph2);
        if(wavePh >= 1.0f)
            wavePh -= 1.0f;
        else if(wavePh < 0.0f)
            wavePh += 1.0f;
    }

    float wave = FastSin01(wavePh);
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