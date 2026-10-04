#include "daisy_seed.h"
#include "daisysp.h"
#include "Utility/delayline.h"
#include "touch/pads.h"
#include "touch/knobs.h"
#include "touch/switches.h"
#include "nuut/voice.h"
#include "nuut/planets.h"
#include "nuut/constellation.h"
#include "nuut/crystals.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

Voice voices[14];
PlanetConstellationVoice constellationVoices[7];
bool constellationVoiceWasActive[7] = {false};

CombLP6BreathState combLP6Breath;

constexpr size_t DELAY_SIZE = 24000;

daisysp::DelayLine<float, DELAY_SIZE> reverbDelayL;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayR;

Pads pads;
Knobs knobs;
Constellation constellation;
Switches switches;
Crystals crystals;

bool padStates[10] = {false};
bool holdEnabled = false;

float transitPosition = 0.0f;

bool AnyPadActive();

void OnPadTouch(uint16_t pad)
{
    // P11 toggles Hold mode.
    if(pad == 11)
    {
        holdEnabled = !holdEnabled;

        if(holdEnabled)
        {
            hw.SetLed(true);
        }
        else
        {
            for(uint16_t i = 0; i < 10; i++)
            {
                padStates[i] = false;
                constellation.ReleasePlanet(i);
            }

            hw.SetLed(false);
        }

        return;
    }

    if(pad < 10)
    {
        if(holdEnabled)
        {
            for(uint16_t i = 0; i < 10; i++)
            {
                padStates[i] = false;
                constellation.ReleasePlanet(i);
            }
        }

        padStates[pad] = true;
        hw.SetLed(true);
        constellation.AssignPlanet(pad);
    }
}

void OnPadRelease(uint16_t pad)
{
    if(pad < 10)
    {
        if(holdEnabled)
        {
            // Hold keeps the selected planet active.
            return;
        }

        padStates[pad] = false;
        constellation.ReleasePlanet(pad);
        hw.SetLed(AnyPadActive());
    }
}

const float attackTime = 0.30f;
const float releaseTime = 1.2f;

float attackIncrement;
float releaseIncrement;
float compressorEnvelope = 0.0f;

bool AnyPadActive()
{
    for(uint16_t i = 0; i < 10; i++)
    {
        if(padStates[i])
            return true;
    }

    return false;
}

void AudioCallback(
    AudioHandle::InputBuffer in,
    AudioHandle::OutputBuffer out,
    size_t size)
{
    float originValue = knobs.s31().Process();
    float arcValue = knobs.s32().Process();

    // Transit
    float transitKnob = knobs.s36().Process();
    int transitMode = switches.B();

    if(transitMode == Switch3::POS_CENTER)
    {
        // OFF: the fader directly controls the orbital position.
        transitPosition = transitKnob;
    }
    else
    {
        // Automatic movement starts from the current position.
        float speed = 0.00005f;

        if(transitMode == Switch3::POS_DOWN)
            speed = 0.00015f;

        transitPosition += speed;

        if(transitPosition > 1.0f)
            transitPosition -= 1.0f;
    }

    constellation.SetTransit(transitPosition);

    // Constellation
    float constellationValue = knobs.s30().Process();
    constellation.SetConstellation(constellationValue);

    int aspectA = -1;
    int aspectB = -1;
    float aspectDistance = 0.0f;

    int currentAspect =
        constellation.FindAspect(
            aspectA,
            aspectB,
            aspectDistance
        );

    AspectData aspect;

    aspect.type = currentAspect;

    switch(currentAspect)
    {
        case ASPECT_CONJUNCTION:
            aspect.intensity = 0.60f;
            break;

        case ASPECT_SEXTILE:
            aspect.shimmer = 0.70f;
            break;

        case ASPECT_SQUARE:
            aspect.tension = 0.70f;
            break;

        case ASPECT_TRINE:
            aspect.shimmer = 0.50f;
            aspect.intensity = 0.40f;
            break;

        case ASPECT_OPPOSITION:
            aspect.tension = 0.80f;
            break;
    }


    if(aspect.type != ASPECT_NONE)
        {
            aspect.proximity =
                constellation.GetAspectProximity(
                    aspectDistance,
                    aspect.type
                );
        }

    bool currentConstellationActive[7] = {false};

    float originSemitones =
        (originValue - 0.5f) * 24.0f;

    float originRatio =
        powf(2.0f, originSemitones / 12.0f);

    for(int v = 3; v < 10; v++)
    {
        int idx = v - 3;
        int planet = constellation.GetPlanet(v);

        if(planet != -1 &&
           constellation.IsExtraVoice(v))
        {
            currentConstellationActive[idx] = true;

            if(!constellationVoiceWasActive[idx])
            {
                constellationVoices[idx].triggered = true;
            }
        }
    }

    // Crystals
    float crystalAura = knobs.s33().Process();
    float crystalRefraction = knobs.s34().Process();
    float crystalRadiance = knobs.s37().Process();
    float crystalWet = knobs.s35().Process();

    for(size_t i = 0; i < size; i++)
    {
      

        float planetSig = 0.0f;
        float constellationSig = 0.0f;

        for(int v = 0; v < 10; v++)
        {
            int planet = constellation.GetPlanet(v);

            if(v >= 3)
            {
                int idx = v - 3;

                if(planet != -1 &&
                   constellation.IsExtraVoice(v) &&
                   AnyPadActive())
                {
                    constellationVoices[idx].active = true;

                    constellationVoices[idx].frequency =
                        padFrequencies[planet] * originRatio;
                }
                else
                {
                    constellationVoices[idx].active = false;
                }

                constellationSig +=
                    ProcessPlanetConstellationVoice(
                        constellationVoices[idx],
                        constellationVoices[idx].frequency
                    );

                continue;
            }

            bool gate = false;

            if(planet != -1)
                gate = padStates[planet];

            planetSig +=
                ProcessVoice(
                    voices[v],
                    gate,
                    0.0f,
                    aspect,
                    originRatio,
                    arcValue,
                    &combLP6Breath
                );
        }

        float sig =
            planetSig + constellationSig;

            // Global reverb
            float reverbL = reverbDelayL.Read();
            float reverbR = reverbDelayR.Read();

            reverbDelayL.Write(
                sig + reverbR * 0.35f
            );

            reverbDelayR.Write(
                sig + reverbL * 0.35f
            );

            sig =
                sig * 0.75f
                + reverbL * 0.15f
                + reverbR * 0.15f;

        float drySig = sig;

        CrystalStereo crystalSig =
            crystals.Process(
                sig,
                switches.A(),
                crystalAura,
                crystalRefraction,
                crystalRadiance,
                AnyPadActive()
            );

        float outputL =
            drySig * (1.0f - crystalWet)
            + crystalSig.left * crystalWet;

        float outputR =
            drySig * (1.0f - crystalWet)
            + crystalSig.right * crystalWet;

        float outputGain = 0.7f;

        outputL *= outputGain;
        outputR *= outputGain;

        outputL = tanhf(outputL);
        outputR = tanhf(outputR);

        out[0][i] = outputL;
        out[1][i] = outputR;
    }

    for(int i = 0; i < 7; i++)
    {
        constellationVoiceWasActive[i] = currentConstellationActive[i];
    }
}

int main()
{
    hw.Configure();
    hw.Init();

    knobs.Init(hw);

    hw.SetLed(true);
    System::Delay(1000);
    hw.SetLed(false);

    pads.Init();
    switches.Init();

    float sampleRate =
        hw.AudioSampleRate();

    InitCombLP6Breath(&combLP6Breath, sampleRate);

    crystals.Init(sampleRate);

   

    reverbDelayL.Init();
    reverbDelayR.Init();

    reverbDelayL.SetDelay(
        sampleRate * 0.31f
    );

    reverbDelayR.SetDelay(
        sampleRate * 0.43f
    );

    for(int i = 0; i < 14; i++)
    {
        InitVoice(
            voices[i],
            sampleRate
        );
    }

    for(int i = 0; i < 7; i++)
    {
        InitPlanetConstellationVoice(
            constellationVoices[i],
            sampleRate
        );
    }

    constellation.Init(
        voices,
        padFrequencies
    );

    attackIncrement =
        1.0f / (attackTime * sampleRate);

    releaseIncrement =
        1.0f / (releaseTime * sampleRate);

    pads.SetOnTouch(OnPadTouch);
    pads.SetOnRelease(OnPadRelease);

    hw.StartAudio(AudioCallback);

    while(true)
    {
        pads.Process();
        System::Delay(4);
    }
}