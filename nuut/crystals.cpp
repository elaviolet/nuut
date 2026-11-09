#include "crystals.h"

#include <cmath>

namespace
{

constexpr float kDelaySlew = 0.0003f;


inline float CrystalFastSin01(float phase)
{
    const float x = 2.0f * phase - 1.0f;
    float y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}

inline float ClampDelay(float samples, float maxSamples)
{
    if(samples < 2.0f)
        return 2.0f;
    if(samples > maxSamples)
        return maxSamples;
    return samples;
}

inline void Advance(float& phase, float inc)
{
    phase += inc;
    if(phase >= 1.0f)
        phase -= 1.0f;
}
}

void Crystals::Init(float sampleRate)
{
    this->sampleRate = sampleRate;
    invSampleRate = 1.0f / sampleRate;

    lineL.Init();
    lineR.Init();

    // Ametista: octave shimmer
    shifter.Init(sampleRate);
    shifter.SetFun(0.0f);
    shifter.SetTransposition(12.0f);

    // Ossidiana: dark resonant filter
    darkFilter.Init(sampleRate);
    darkFilter.SetFreq(450.0f);
    darkFilter.SetRes(0.4f);

    // Acquamarina: filtered water noise
    noise.Init();
    waterFilter.Init(sampleRate);
    waterFilter.SetFreq(1200.0f);
    waterFilter.SetRes(0.2f);

    delayL = 0.30f * sampleRate;
    delayR = 0.30f * sampleRate;

    lfoPhaseA = 0.0f;
    lfoPhaseB = 0.37f;

    shimmerDamp = 0.0f;
    dampL = 0.0f;
    dampR = 0.0f;
    loopDamp = 0.0f;

    counter = 0;
    lastMode = -1;
}

CrystalStereo Crystals::Process(
    float input,
    int mode,
    float refraction,
    float radiance,
    bool active)
{
    const float maxDelay = static_cast<float>(kLineSize - 2);

    ++counter;
    lastMode = mode;

    CrystalStereo result{input, input};

    // --------------------------------------------------------
    // AMETISTA: shimmering, spatial octave halo.
    // The pitch shifter sits inside the feedback loop, so every
    // repeat climbs another octave and the damping filter melts
    // the top end away. Two unsynchronised wobbles on the L/R
    // delay times keep the tail moving and wide.
    // Loop gain is at most 0.62, so it cannot run away.
    // --------------------------------------------------------
    if(mode == daisy::Switch3::POS_CENTER)
    {
        Advance(lfoPhaseA, 0.17f * invSampleRate);
        Advance(lfoPhaseB, 0.23f * invSampleRate);

        const float modA = CrystalFastSin01(lfoPhaseA);
        const float modB = CrystalFastSin01(lfoPhaseB);

        delayL += (0.29f * sampleRate - delayL) * kDelaySlew;
        delayR += (0.41f * sampleRate - delayR) * kDelaySlew;

        const float wobble = 0.0025f * sampleRate;
        const float tapL
            = lineL.Read(ClampDelay(delayL + modA * wobble, maxDelay));
        const float tapR
            = lineR.Read(ClampDelay(delayR + modB * wobble, maxDelay));

        const float feedback = 0.30f + refraction * 0.32f;
        const float fbMono   = (tapL + tapR) * 0.5f;

        float shifterIn = input * 0.6f + fbMono * feedback;
        const float shifted = shifter.Process(shifterIn);

        shimmerDamp += 0.40f * (shifted - shimmerDamp);

        lineL.Write(shimmerDamp);
        lineR.Write(shimmerDamp);

        // A little immediate octave plus the two wide, drifting tails.
        const float halo = shimmerDamp * 0.25f;

        result = {
            input + halo + tapL * 0.70f,
            input + halo + tapR * 0.70f
        };
    }

    // --------------------------------------------------------
    // ACQUAMARINA: nautical ping-pong.
    // Echoes alternate left and right, each repeat is rounded
    // off like sound travelling through water, and the delay
    // time sways in opposite directions on the two sides.
    // A faint, slowly pulsing water noise is kept from before.
    // --------------------------------------------------------
    else if(mode == daisy::Switch3::POS_UP)
    {
        Advance(lfoPhaseA, 0.14f * invSampleRate);
        const float sway = CrystalFastSin01(lfoPhaseA);

        // Pulsing water-noise envelope
        const float wave = 0.5f + 0.5f * sway;
        float noiseEnvelope = wave * wave;
        noiseEnvelope *= noiseEnvelope;

        const float n = noise.Process();
        waterFilter.Process(n);
        const float noiseAmount
            = (0.003f + radiance * 0.005f) * noiseEnvelope;
        const float noiseSig
            = active ? waterFilter.Low() * noiseAmount : 0.0f;

        delayL += (0.36f * sampleRate - delayL) * kDelaySlew;
        delayR += (0.36f * sampleRate - delayR) * kDelaySlew;

        const float swaySamples = sway * 0.004f * sampleRate;
        const float tapL
            = lineL.Read(ClampDelay(delayL + swaySamples, maxDelay));
        const float tapR
            = lineR.Read(ClampDelay(delayR - swaySamples, maxDelay));

        const float feedback = 0.38f + refraction * 0.32f;

        // Water absorbs the highs a bit more on every bounce.
        dampL += 0.30f * (tapL - dampL);
        dampR += 0.30f * (tapR - dampR);

        // Ping-pong: input enters the left line, the right line only
        // hears the left echoes, and the left line hears the right ones.
        lineL.Write(input + noiseSig + dampR * feedback);
        lineR.Write(dampL * feedback);

        result = {
            input * 0.85f + tapL * 0.55f + noiseSig,
            input * 0.85f + tapR * 0.55f + noiseSig
        };
    }

    // --------------------------------------------------------
    // OSSIDIANA: dark, resonant, slowly breathing.
    // A low-pass with a slow sweep sits outside the loop; a
    // separate one-pole in the feedback makes every repeat
    // darker. The resonant filter is not in the feedback path,
    // so it cannot self-oscillate.
    // --------------------------------------------------------
    else if(mode == daisy::Switch3::POS_DOWN)
    {
        Advance(lfoPhaseA, 0.07f * invSampleRate);
        Advance(lfoPhaseB, 0.11f * invSampleRate);

        const float sweep = 0.5f + 0.5f * CrystalFastSin01(lfoPhaseA);
        const float modB  = CrystalFastSin01(lfoPhaseB);

        delayL += (0.37f * sampleRate - delayL) * kDelaySlew;
        delayR += (0.53f * sampleRate - delayR) * kDelaySlew;

        const float wobble = 0.002f * sampleRate;
        const float tapL
            = lineL.Read(ClampDelay(delayL + modB * wobble, maxDelay));
        const float tapR
            = lineR.Read(ClampDelay(delayR - modB * wobble, maxDelay));

        const float fbMono   = (tapL + tapR) * 0.5f;
        const float feedback = 0.50f + radiance * 0.20f;

        loopDamp += 0.18f * (fbMono - loopDamp);

        const float x = input + loopDamp * feedback;
        lineL.Write(x);
        lineR.Write(x);

        // Slow cutoff sweep, updated every 32 samples.
        if((counter & 31u) == 0u)
        {
            darkFilter.SetFreq(300.0f + sweep * 420.0f);
            darkFilter.SetRes(0.40f + radiance * 0.20f);
        }

        darkFilter.Process(input + fbMono * (0.40f + radiance * 0.15f));
        const float dark = darkFilter.Low();

        const float body = 0.40f + refraction * 0.25f;

        result = {
            input * 0.60f + dark * body + tapL * 0.28f,
            input * 0.60f + dark * body + tapR * 0.28f
        };
    }

    return result;
}