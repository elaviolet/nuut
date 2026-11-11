#include "crystals.h"

// How much lighter the shimmer is made: 1 = full rate (heaviest),
// 2 = half, 3 = a third, 4 = a quarter. 2 keeps the shimmer clean; 3 is
// lighter but can sound rougher.
static constexpr int kShimmerDecimate = 3;

#include <cmath>

namespace
{
// How fast the delay times glide to their target (per sample).
constexpr float kDelaySlew = 0.0003f;

// Cheap sine for a phase in [0, 1) (parabolic approximation + refinement).
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

// One sea wave over a phase in [0, 1): it builds up slowly, breaks, then
// the foam fades away for a long time (an asymmetric swell).
inline float WaveShape(float p)
{
    if(p < 0.38f)
    {
        const float x = p / 0.38f;
        return x * x * (3.0f - 2.0f * x);
    }

    const float x = (p - 0.38f) / 0.62f;
    const float y = 1.0f - x;
    return y * y * y;
}

// Left/right gains for a slow pan: pan in [-1, 1], width in [0, 1].
// At width 1 each side swings between silence and full level.
inline void PanGains(float pan, float width, float& gL, float& gR)
{
    const float comp = 1.0f + width * 0.45f;
    gL = (1.0f - width * 0.5f * (1.0f + pan)) * comp;
    gR = (1.0f - width * 0.5f * (1.0f - pan)) * comp;
}

inline float NoiseFromState(unsigned& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int>(state)) * 4.6566129e-10f;
}

inline void Advance(float& phase, float inc)
{
    phase += inc;
    if(phase >= 1.0f)
        phase -= 1.0f;
}
} // namespace

void Crystals::Init(float sampleRate)
{
    this->sampleRate = sampleRate;
    invSampleRate = 1.0f / sampleRate;

    lineL.Init();
    lineR.Init();

    // Ametista: octave shimmer
    // The shimmer runs at half the sample rate: it is the heaviest part of
    // the whole effect and the top end is rounded off anyway.
    shifter.Init(sampleRate / static_cast<float>(kShimmerDecimate));
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
    shifterAccum = shifterPrev = shifterCur = 0.0f;
    shifterPhase = 0;
    dampL = 0.0f;
    dampR = 0.0f;
    loopDamp = 0.0f;
    shimmerLow = 0.0f;
    lfoPhaseC = 0.71f;
    alchemySmooth = 0.0f;
    panPhase = 0.0f;
    movement = 0.0f;
    movementSmooth = 0.0f;
    inLp = 0.0f;
    hpL = 0.0f;
    hpR = 0.0f;
    wavePhaseA = 0.0f;
    wavePhaseB = 0.41f;
    waveLpL1 = waveLpL2 = waveLpR1 = waveLpR2 = 0.0f;
    waveK = 0.05f;
    waveGain = 1.0f;
    waveGate = 0.0f;

    counter = 0;
    lastMode = -1;
}

CrystalStereo Crystals::Process(
    float input,
    int mode,
    float refraction,
    float radiance,
    bool active,
    float alchemy)
{
    // Alchemy (0..1) makes every crystal more explosive: longer and
    // brighter tails, deeper movement, a wider stereo image.
    // It is smoothed here, per sample, so that moving the fader does not
    // make the delay times or the filters jump (that sounds like crackle).

    constexpr bool kCrystalsUseAlchemy = true;
    const float aTarget
        = !kCrystalsUseAlchemy ? 0.0f
          : (alchemy < 0.0f ? 0.0f : (alchemy > 1.0f ? 1.0f : alchemy));
    alchemySmooth += 0.0004f * (aTarget - alchemySmooth);
    const float a = alchemySmooth;

    // While origin / Kosmos / arc are moving, the old echoes would repeat
    // the gliding note as a descending staircase. So the feedback is
    // cut quickly while moving and comes back slowly afterwards.
    movementSmooth += (movement > movementSmooth ? 0.004f : 0.00004f)
                      * (movement - movementSmooth);
    const float duck = 1.0f - 0.92f * movementSmooth;

    const float maxDelay = static_cast<float>(kLineSize - 2);

    // Radiance = stereo width of every crystal: how far the sound
    // travels from left to right. Never fully centred.
    const float width = 0.30f + 0.70f * radiance;

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
        Advance(lfoPhaseA, (0.17f + a * 0.20f) * invSampleRate);
        Advance(lfoPhaseB, (0.23f + a * 0.25f) * invSampleRate);
        Advance(lfoPhaseC, 0.043f * invSampleRate);

        const float modA = CrystalFastSin01(lfoPhaseA);
        const float modB = CrystalFastSin01(lfoPhaseB);

        // The shimmer stays exactly one octave up: no pitch wobble.

        delayL += (0.29f * sampleRate - delayL) * kDelaySlew;
        delayR += (0.41f * sampleRate - delayR) * kDelaySlew;

        const float wobble = (0.0025f + a * 0.006f) * sampleRate;
        const float tapL
            = lineL.Read(ClampDelay(delayL + modA * wobble, maxDelay));
        const float tapR
            = lineR.Read(ClampDelay(delayR + modB * wobble, maxDelay));

        // Loop gain stays below 0.8, so it cannot run away.
        const float feedback = (0.30f + refraction * 0.32f + a * 0.16f) * duck;
        const float fbMono   = (tapL + tapR) * 0.5f;

        float shifterIn = input * 0.6f + fbMono * feedback;
        // Reduced-rate shifter: feed it the average of kShimmerDecimate
        // samples, and slide between its outputs.
        shifterAccum += shifterIn;
        if(++shifterPhase >= kShimmerDecimate)
        {
            shifterPhase = 0;
            shifterPrev  = shifterCur;
            float shifterHalfIn
                = shifterAccum / static_cast<float>(kShimmerDecimate);
            shifterCur   = shifter.Process(shifterHalfIn);   // wants a float&
            shifterAccum = 0.0f;
        }
        const float shifted
            = shifterPrev
              + (shifterCur - shifterPrev)
                    * (static_cast<float>(shifterPhase)
                       / static_cast<float>(kShimmerDecimate));

        shimmerDamp += 0.40f * (shifted - shimmerDamp);

        // Take the low rumble (below about 150 Hz) out of the loop: it
        // would pile up and turn into a hum.
        shimmerLow += 0.02f * (shimmerDamp - shimmerLow);
        const float shimmerClean = shimmerDamp - shimmerLow;

        lineL.Write(shimmerClean);
        lineR.Write(shimmerClean);

        // A little immediate octave plus the two wide, drifting tails.
        const float halo = shimmerClean * (0.20f + a * 0.10f);
        const float tails = 0.60f + a * 0.08f;

        result = {
            // The halo stays in the middle, the echoes land hard on one
            // side and then the other (0.29 s left, 0.41 s right).
            input + halo * (1.0f - 0.5f * width) + tapL * tails * (0.8f + 0.5f * width),
            input + halo * (1.0f - 0.5f * width) + tapR * tails * (0.8f + 0.5f * width)
        };
    }

    // --------------------------------------------------------
    // ACQUAMARINA: nautical, spacious ping-pong with a real sea.
    // - Echoes bounce left and right at two different times, so the
    //   pattern keeps moving across the stereo field. Every bounce
    //   loses its lows and highs, like sound through water.
    // - The delay times sway like an underwater chorus.
    // - Underneath, waves of filtered noise swell and break: two wave
    //   trains of about 9 s and 13 s, brighter at the crest, with a
    //   different noise on each side. Alchemy and Radiance make the sea
    //   louder and rougher.
    // --------------------------------------------------------
    else if(mode == daisy::Switch3::POS_UP)
    {
        Advance(lfoPhaseA, (0.30f + a * 0.25f) * invSampleRate);
        const float sway = CrystalFastSin01(lfoPhaseA);

        // Sea waves
        // Slow, relaxed waves (about 18 s and 25 s). The right side
        // gets the same waves a little later, so the sea rolls across.
        Advance(wavePhaseA, 0.055f * invSampleRate);
        Advance(wavePhaseB, 0.040f * invSampleRate);
        float phaseAR = wavePhaseA + 0.30f;
        float phaseBR = wavePhaseB + 0.55f;
        if(phaseAR >= 1.0f)
            phaseAR -= 1.0f;
        if(phaseBR >= 1.0f)
            phaseBR -= 1.0f;
        const float swellL
            = 0.60f * WaveShape(wavePhaseA) + 0.50f * WaveShape(wavePhaseB);
        const float swellR
            = 0.60f * WaveShape(phaseAR) + 0.50f * WaveShape(phaseBR);
        const float swell = 0.5f * (swellL + swellR);

        // Brighter at the crest of the wave (cutoff updated every 32 samples).
        if((counter & 31u) == 0u)
        {
            const float fc = 90.0f + 420.0f * swell;
            waveK = 1.0f - expf(-6.2831853f * fc * invSampleRate);
            // Keeps the noise level about the same at every cutoff.
            waveGain = 0.75f / sqrtf(waveK + 0.01f);
        }

        // The sea is only there while a planet is played; it fades in
        // fast and washes out slowly.
        const float gateTarget = active ? 1.0f : 0.0f;
        waveGate += (gateTarget > waveGate ? 0.00008f : 0.00001f)
                    * (gateTarget - waveGate);

        waveLpL1 += waveK * (NoiseFromState(waveRngL) - waveLpL1);
        waveLpL2 += waveK * (waveLpL1 - waveLpL2);
        waveLpR1 += waveK * (NoiseFromState(waveRngR) - waveLpR1);
        waveLpR2 += waveK * (waveLpR1 - waveLpR2);

        const float seaLevel
            = (0.14f + a * 0.08f) * waveGate;
        const float seaL = waveLpL2 * waveGain * seaLevel * swellL;
        const float seaR = waveLpR2 * waveGain * seaLevel * swellR;

        // Ping-pong at two different times.
        delayL += (0.21f * sampleRate - delayL) * kDelaySlew;
        delayR += (0.31f * sampleRate - delayR) * kDelaySlew;

        const float swaySamples = sway * (0.005f + a * 0.008f) * sampleRate;
        const float tapL
            = lineL.Read(ClampDelay(delayL + swaySamples, maxDelay));
        const float tapR
            = lineR.Read(ClampDelay(delayR - swaySamples, maxDelay));

        const float feedback = (0.45f + refraction * 0.30f + a * 0.12f) * duck;

        // Each bounce: the highs are absorbed (low-pass) and the lows
        // thin out (high-pass), so the echoes get airy and watery.
        dampL += 0.55f * (tapL - dampL);
        dampR += 0.55f * (tapR - dampR);
        hpL += 0.06f * (dampL - hpL);   // thin, airy: lows are removed
        hpR += 0.06f * (dampR - hpR);

        // What goes into the echoes is smoothed first: raw clicks of the
        // resonators would otherwise repeat as a crackle.
        inLp += 0.35f * (input - inLp);

        lineL.Write(inLp + (dampR - hpR) * feedback);
        lineR.Write((dampL - hpL) * feedback);

        // The wider Radiance is, the harder the echoes bounce to one side.
        // Real ping-pong: one echo hard left, the next hard right.
        const float cross = 0.10f * (1.0f - width);
        result = {
            input * 0.80f + tapL * 0.80f + tapR * cross + seaL,
            input * 0.80f + tapR * 0.80f + tapL * cross + seaR
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
        Advance(lfoPhaseA, (0.07f + a * 0.10f) * invSampleRate);
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
        const float feedback = (0.50f + radiance * 0.12f + a * 0.10f) * duck;

        loopDamp += 0.07f * (fbMono - loopDamp);

        // Smoothed input: no clicks repeating in the dark echoes.
        inLp += 0.12f * (input - inLp);
        // The loop is dark but must not pile up low rumble.
        hpL += 0.015f * (loopDamp - hpL);
        const float x = inLp + (loopDamp - hpL) * feedback;
        lineL.Write(x);
        lineR.Write(x);

        // Slow cutoff sweep, updated every 32 samples.
        if((counter & 31u) == 0u)
        {
            darkFilter.SetFreq(300.0f + sweep * (420.0f + a * 500.0f));
            darkFilter.SetRes(0.40f + radiance * 0.15f + a * 0.10f);
        }

        darkFilter.Process(input + fbMono * (0.40f + radiance * 0.15f));
        const float dark = darkFilter.Low();

        const float body = 0.40f + refraction * 0.25f;

        // The dark body stays in the middle; the two echoes land hard
        // left (0.37 s) and hard right (0.53 s).
        const float echo = 0.28f + 0.22f * width;

        result = {
            input * 0.60f + dark * body + tapL * echo,
            input * 0.60f + dark * body + tapR * echo
        };
    }

    // Gentle soft limiter: loud passages round off instead of clipping.
    result.left  = result.left / (1.0f + 0.60f * fabsf(result.left));
    result.right = result.right / (1.0f + 0.60f * fabsf(result.right));

    return result;
}