#pragma once

#include "daisy_seed.h"
#include "daisysp.h"

#include <cstddef>

struct CrystalStereo
{
    float left;
    float right;
};

// Three stereo effects selected by the A switch:
//   POS_CENTER : Ametista    - octave shimmer, wide and spatial
//   POS_UP     : Acquamarina - nautical ping-pong delay
//   POS_DOWN   : Ossidiana   - dark, resonant, slowly sweeping
//
// Mono in, stereo out. The output includes the input signal;
// the caller blends it with the dry signal using the wet knob.
class Crystals
{
public:
    void Init(float sampleRate);

    // 0..1: how fast origin / Kosmos / arc are being moved right now.
    void SetMovement(float m) { movement = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m); }

    CrystalStereo Process(
        float input,
        int mode,
        float refraction,
        float radiance,
        bool active,
        float alchemy = 0.0f   // 0..1: the more, the more explosive
    );

private:
    static constexpr std::size_t kLineSize = 28000; // about 0.58 s at 48 kHz

    // Stereo delay pair shared by the three modes
    daisysp::DelayLine<float, kLineSize> lineL;
    daisysp::DelayLine<float, kLineSize> lineR;

    // Ametista: octave shimmer inside the feedback loop
    daisysp::PitchShifter shifter;

    // Ossidiana: dark resonant filter
    daisysp::Svf darkFilter;

    // Acquamarina: filtered water noise
    daisysp::Svf waterFilter;
    daisysp::WhiteNoise noise;

    float sampleRate = 48000.0f;
    float invSampleRate = 1.0f / 48000.0f;

    // Smoothed delay times (in samples) so switching modes glides
    float delayL = 14400.0f;
    float delayR = 14400.0f;

    // Slow modulation phases
    float lfoPhaseA = 0.0f;
    float lfoPhaseB = 0.37f;

    // Feedback damping states
    float shimmerDamp = 0.0f;
    float shifterAccum = 0.0f;
    float shifterPrev = 0.0f;
    float shifterCur = 0.0f;
    int shifterPhase = 0;
    float dampL = 0.0f;
    float dampR = 0.0f;
    float loopDamp = 0.0f;
    float shimmerLow = 0.0f;   // low-pass used to take the rumble out of the loop
    float lfoPhaseC = 0.71f;

    // Alchemy, smoothed per sample so that moving the fader never crackles.
    float alchemySmooth = 0.0f;
    float panPhase = 0.0f;
    float movement = 0.0f;
    float movementSmooth = 0.0f;
    float inLp = 0.0f;   // smooths the signal written into the echo lines

    // Acquamarina: high-pass states of the echoes and the sea waves.
    float hpL = 0.0f;
    float hpR = 0.0f;
    float wavePhaseA = 0.0f;
    float wavePhaseB = 0.41f;
    float waveLpL1 = 0.0f;
    float waveLpL2 = 0.0f;
    float waveLpR1 = 0.0f;
    float waveLpR2 = 0.0f;
    float waveK = 0.05f;
    float waveGain = 1.0f;
    float waveGate = 0.0f;
    unsigned waveRngL = 12345u;
    unsigned waveRngR = 67890u;

    unsigned counter = 0;
    float amethystModA = 0.0f;
    float amethystModB = 0.0f;
    int lastMode = -1;
};