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
constexpr size_t DELAY_SIZE = 24000;

daisysp::DelayLine<float, DELAY_SIZE> reverbDelayL;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayR;

Pads pads;
Knobs knobs;
Constellation constellation;
Switches switches;
Crystals crystals;

bool padStates[10] = {false};

Oscillator filterLfo;

bool AnyPadActive();

// Pad touched
void OnPadTouch(uint16_t pad)
{
    if(pad < 10)
    {
        padStates[pad] = true;

        hw.SetLed(true);

        constellation.AssignPlanet(pad);
    }
}

// Pad released
void OnPadRelease(uint16_t pad)
{
    if(pad < 10)
    {
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

// AudioCallback begin

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{

    float originValue = knobs.s31().Process();
    float arcValue = knobs.s32().Process();

    //Transit
    float transitValue = knobs.s36().Process();
    constellation.SetTransit(transitValue);

    //Constellation
    float constellationValue = knobs.s30().Process();
    constellation.SetConstellation(constellationValue);

    int oppositionA = -1;
    int oppositionB = -1;
    float oppositionProximity = 0.0f;

    bool hasOpposition =
        constellation.FindOpposition(
            oppositionA,
            oppositionB,
            oppositionProximity
        );

    bool currentConstellationActive[7] = {false};

    float originSemitones = (originValue - 0.5f) * 24.0f;
    float originRatio = powf(2.0f, originSemitones / 12.0f);

    for(int v = 3; v < 10; v++)
    {
        int idx = v - 3;
        int planet = constellation.GetPlanet(v);

        if(planet != -1 && constellation.IsExtraVoice(v))
        {
            currentConstellationActive[idx] = true;

            if(!constellationVoiceWasActive[idx])
            {
                constellationVoices[idx].triggered = true;
            }
        }
    }

    //crystals
    float crystalAura = knobs.s33().Process();
    float crystalRefraction = knobs.s34().Process();
    float crystalRadiance = knobs.s37().Process();
    float crystalWet = knobs.s35().Process();

    for(size_t i = 0; i < size; i++)
    {
        float filterMod =
            (filterLfo.Process() + 1.0f) * 0.5f;

        if(hasOpposition)
        {
            filterMod += oppositionProximity * 0.5f;
        }

        float planetSig = 0.0f;
        float constellationSig = 0.0f;

        for(int v = 0; v < 10; v++)
        {
            int planet = constellation.GetPlanet(v);

            if(v >= 3)
            {
                int idx = v - 3;

                if(planet != -1 &&
                    constellation.IsExtraVoice(v) && AnyPadActive())
                    {
                        constellationVoices[idx].active = true;
                        constellationVoices[idx].frequency =
                            padFrequencies[planet] * originRatio;
                    }
                    else
                    {
                        constellationVoices[idx].active = false;
                    }

                constellationSig += ProcessPlanetConstellationVoice(
                    constellationVoices[idx],
                    constellationVoices[idx].frequency
                );

                continue;
            }

            bool gate = false;

            if(planet != -1)
                gate = padStates[planet];

            planetSig += ProcessVoice(
                voices[v],
                gate,
                filterMod,
                oppositionProximity,
                originRatio,
                arcValue
            );
        }

        float sig = planetSig + constellationSig;

        float drySig = sig;

        CrystalStereo crystalSig = crystals.Process(sig, switches.A(), crystalAura, crystalRefraction, crystalRadiance, AnyPadActive());

        float outputL =
            drySig * (1.0f - crystalWet)
            + crystalSig.left * crystalWet;

        float outputR =
            drySig * (1.0f - crystalWet)
            + crystalSig.right * crystalWet;

        float outputGain = 0.7f;

        outputL *= outputGain;
        outputR *= outputGain;

        if(outputL > 0.95f)
            outputL = 0.95f;
        else if(outputL < -0.95f)
            outputL = -0.95f;

        if(outputR > 0.95f)
            outputR = 0.95f;
        else if(outputR < -0.95f)
            outputR = -0.95f;

        out[0][i] = outputL;
        out[1][i] = outputR;
    }

    for(int i = 0; i < 7; i++)
    {
        constellationVoiceWasActive[i] =
            currentConstellationActive[i];
    }

    
}
// ACend

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
    float sampleRate = hw.AudioSampleRate();

    crystals.Init(sampleRate);

    filterLfo.Init(sampleRate);
    filterLfo.SetWaveform(Oscillator::WAVE_SIN);
    filterLfo.SetFreq(0.08f);
    filterLfo.SetAmp(1.0f);

    reverbDelayL.Init();
    reverbDelayR.Init();

    reverbDelayL.SetDelay(sampleRate * 0.31f);
    reverbDelayR.SetDelay(sampleRate * 0.43f);

    for(int i = 0; i < 14; i++)
    {
        InitVoice(voices[i], sampleRate);
    }

    for(int i = 0; i < 7; i++)
    {
        InitPlanetConstellationVoice(
            constellationVoices[i],
            sampleRate
        );
    }

    // Inizializza la costellazione
    // Initialize the constellation
    constellation.Init(voices, padFrequencies);

    attackIncrement = 1.0f / (attackTime * sampleRate);
    releaseIncrement = 1.0f / (releaseTime * sampleRate);

    pads.SetOnTouch(OnPadTouch);
    pads.SetOnRelease(OnPadRelease);

    hw.StartAudio(AudioCallback);

    while(true)
    {
        pads.Process();
        System::Delay(4);
    }
}