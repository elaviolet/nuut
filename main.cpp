#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;
Oscillator osc;

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    for(size_t i = 0; i < size; i++)
    {
        float sig = osc.Process();

        out[0][i] = sig;
        out[1][i] = sig;
    }
}

int main()
{
    hw.Configure();
    hw.Init();

    osc.Init(hw.AudioSampleRate());
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(220.0f);
    osc.SetAmp(0.2f);

    hw.StartAudio(AudioCallback);

    while(true)
    {
    }
}