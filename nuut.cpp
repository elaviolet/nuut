#include "daisy_seed.h"
#include "daisysp.h"
#include "Utility/delayline.h"
#include "touch/pads.h"
#include "touch/knobs.h"
#include "nuut/voice.h"
#include "nuut/planets.h"
#include "nuut/constellation.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;


Voice voices[3];
constexpr size_t DELAY_SIZE = 24000;

daisysp::DelayLine<float, DELAY_SIZE> reverbDelayL;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayR;

Pads pads;
Knobs knobs;
Constellation constellation;
bool padStates[10] = {false};

Oscillator filterLfo;

bool AnyPadActive();

// Pad premuto
// Pad touched
void OnPadTouch(uint16_t pad)
{
    if(pad < 10)
    {
        padStates[pad] = true;

        constellation.AssignPlanet(pad);

        hw.SetLed(true);
    }
}

// Pad rilasciato
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

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
    {

    float transitValue = knobs.s36().Process();
    constellation.SetTransit(transitValue);

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

    for(size_t i = 0; i < size; i++)
    {
        float filterMod = (filterLfo.Process() + 1.0f) * 0.5f;

        if(hasOpposition)
        {
            filterMod += oppositionProximity * 0.5f;
        }

        float sig = 0.0f;

        for(int v = 0; v < 3; v++)
        {
            bool gate = false;
            int planet = constellation.GetPlanet(v);

            if(planet != -1)
            {
                if(constellation.IsExtraVoice(v))
                    gate = AnyPadActive();
                else
                    gate = padStates[planet];
            }

            sig += ProcessVoice(
            voices[v],
            gate,
            filterMod,
            oppositionProximity
        );
        }

        sig *= 0.3f;

        float inputLevel = fabsf(sig);

        // Envelope follower
        float attackCoeff = 0.001f;
        float releaseCoeff = 0.0001f;

        if(inputLevel > compressorEnvelope)
            compressorEnvelope += (inputLevel - compressorEnvelope) * attackCoeff;
        else
            compressorEnvelope += (inputLevel - compressorEnvelope) * releaseCoeff;

        // Compressione
        float threshold = 0.25f;
        float ratio = 3.0f;

        float gain = 1.0f;

        if(compressorEnvelope > threshold)
        {
            float compressedLevel =
                threshold + (compressorEnvelope - threshold) / ratio;

            gain = compressedLevel / compressorEnvelope;
        }

        sig *= gain;

        float feedback = 0.35f;

        reverbDelayL.Write(sig + reverbDelayL.Read() * feedback);
        reverbDelayR.Write(sig + reverbDelayR.Read() * feedback);

        float delayedL = reverbDelayL.Read();
        float delayedR = reverbDelayR.Read();

        float outputL = sig * 0.8f + delayedL * 0.2f;
        float outputR = sig * 0.8f + delayedR * 0.2f;

        out[0][i] = outputL;
        out[1][i] = outputR;
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
    float sampleRate = hw.AudioSampleRate();

    filterLfo.Init(sampleRate);
    filterLfo.SetWaveform(Oscillator::WAVE_SIN);
    filterLfo.SetFreq(0.08f);
    filterLfo.SetAmp(1.0f);

    reverbDelayL.Init();
    reverbDelayR.Init();

    reverbDelayL.SetDelay(sampleRate * 0.31f);
    reverbDelayR.SetDelay(sampleRate * 0.43f);

    for(int i = 0; i < 3; i++)
    {
        InitVoice(voices[i], sampleRate);
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