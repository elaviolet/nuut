#include "daisy_seed.h"
#include "daisysp.h"
#include "Utility/delayline.h"
#include "touch/pads.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

struct Voice
{
    Oscillator osc1;
    Oscillator osc2;
    Oscillator osc3;

    Oscillator fmOsc;

    Svf filter;

    float envelope = 0.0f;

    bool active = false;
    bool gate = false;

    float frequency = 432.0f;
    float osc2BaseFrequency = 432.0f;
};

float padFrequencies[12] =
{
    261.63f,
    293.66f,
    329.63f,
    349.23f,
    392.00f,
    440.00f,
    493.88f,
    523.25f,
    587.33f,
    659.25f,
    698.46f,
    783.99f
};

Voice voices[3];
constexpr size_t DELAY_SIZE = 24000;

daisysp::DelayLine<float, DELAY_SIZE> reverbDelayL;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayR;

Pads pads;
bool padStates[12] = {false};
int voicePad[3] = {-1, -1, -1};

Oscillator filterLfo;

// Pad premuto
void OnPadTouch(uint16_t pad)
{
    if(pad < 12)
    {
        padStates[pad] = true;

        for(int v = 0; v < 3; v++)
        {
            if(voicePad[v] == -1)
            {
                voicePad[v] = pad;

                float baseFreq = padFrequencies[pad];
                voices[v].frequency = baseFreq;

                if(v == 0)
                    {
                        // Voce 1: fondamentale
                        voices[v].osc1.SetFreq(baseFreq);
                        voices[v].osc2.SetFreq(baseFreq * 2.0f);
                        voices[v].osc2BaseFrequency = baseFreq * 2.0f;
                        voices[v].osc3.SetFreq(baseFreq * 3.0f);
                    }
                    else if(v == 1)
                    {
                        // Voce 2: quinta
                        voices[v].osc1.SetFreq(baseFreq * 1.5f);
                        voices[v].osc2.SetFreq(baseFreq * 3.0f);
                        voices[v].osc2BaseFrequency = baseFreq * 3.0f;
                        voices[v].osc3.SetFreq(baseFreq * 4.5f);
                    }
                    else if(v == 2)
                    {
                        // Voce 3: ottava
                        voices[v].osc1.SetFreq(baseFreq * 2.0f);
                        voices[v].osc2.SetFreq(baseFreq * 4.0f);
                        voices[v].osc2BaseFrequency = baseFreq * 4.0f;
                        voices[v].osc3.SetFreq(baseFreq * 6.0f);
                    }

                break;
            }
        }

        hw.SetLed(true);
    }
}

// Pad rilasciati
void OnPadRelease(uint16_t pad)
{
    if(pad < 12)
    {
        padStates[pad] = false;

        for(int v = 0; v < 3; v++)
        {
            if(voicePad[v] == pad)
            {
                voicePad[v] = -1;
                voices[v].gate = false;
            }
        }

        bool anyPadActive = false;

        for(uint16_t i = 0; i < 12; i++)
        {
            if(padStates[i])
            {
                anyPadActive = true;
                break;
            }
        }

        hw.SetLed(anyPadActive);
    }
}

const float attackTime = 0.30f;
const float releaseTime = 1.2f;


float attackIncrement;
float releaseIncrement;

float compressorEnvelope = 0.0f;

bool AnyPadActive()
{
    for(uint16_t i = 0; i < 12; i++)
    {
        if(padStates[i])
            return true;
    }

    return false;
}

void InitVoice(Voice& v, float sampleRate)
{
    v.osc1.Init(sampleRate);
    v.osc1.SetWaveform(Oscillator::WAVE_SAW);
    v.osc1.SetFreq(216.0f);
    v.osc1.SetAmp(0.2f);

    v.osc2.Init(sampleRate);
    v.osc2.SetWaveform(Oscillator::WAVE_RAMP);
    v.osc2.SetFreq(432.5f);
    v.osc2.SetAmp(0.2f);

    v.osc3.Init(sampleRate);
    v.osc3.SetWaveform(Oscillator::WAVE_SAW);
    v.osc3.SetFreq(647.0f);
    v.osc3.SetAmp(0.2f);

    v.fmOsc.Init(sampleRate);
    v.fmOsc.SetWaveform(Oscillator::WAVE_SIN);
    v.fmOsc.SetFreq(2.0f);
    v.fmOsc.SetAmp(1.0f);

    v.filter.Init(sampleRate);
    v.filter.SetFreq(1100.0f);
    v.filter.SetRes(0.2f);
}

float ProcessVoice(Voice& v, bool gate, float filterMod)
{
    float sig1 = v.osc1.Process();
    float fmMod = v.fmOsc.Process() * 8.0f;
    v.osc2.SetFreq(v.osc2BaseFrequency + fmMod);
    float sig2 = v.osc2.Process();
    float sig3 = v.osc3.Process();
    float sig = (sig1 + sig2 + sig3) * 0.25f;

    // Envelope
    if(gate)
    {
        v.envelope += attackIncrement;

        if(v.envelope >= 1.0f)
            v.envelope = 1.0f;
    }
    else
    {
        v.envelope -= releaseIncrement;

        if(v.envelope <= 0.0f)
            v.envelope = 0.0f;
    }

    sig *= v.envelope;

    // Filtro
    float filterFreq = 700.0f + filterMod * 600.0f;

    if(filterFreq < 100.0f)
        filterFreq = 100.0f;

    v.filter.SetFreq(filterFreq);
    v.filter.Process(sig);

    sig = v.filter.Low();

    return sig;
}

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{

    

    for(size_t i = 0; i < size; i++)
    {
        float filterMod = (filterLfo.Process() + 1.0f) * 0.5f;

        float sig = 0.0f;

        for(int v = 0; v < 3; v++)
        {
            bool gate = false;

            if(voicePad[v] != -1)
            {
                gate = padStates[voicePad[v]];
            }

            sig += ProcessVoice(voices[v], gate, filterMod);
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

    hw.SetLed(true);
    System::Delay(1000);
    hw.SetLed(false);

    // Inizializzazione pad
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