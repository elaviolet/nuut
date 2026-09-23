#include "knobs.h"

using namespace daisy;

void Knobs::Init(DaisySeed& hw)
{
    constexpr size_t knob_count = 8;

    AdcChannelConfig cfg[knob_count];

    cfg[0].InitSingle(seed::A0);
    cfg[1].InitSingle(seed::A1);
    cfg[2].InitSingle(seed::A2);
    cfg[3].InitSingle(seed::A3);
    cfg[4].InitSingle(seed::A4);
    cfg[5].InitSingle(seed::A5);
    cfg[6].InitSingle(seed::A6);
    cfg[7].InitSingle(seed::A7);

    hw.adc.Init(cfg, knob_count);
    hw.adc.Start();

    for(size_t i = 0; i < knob_count; i++)
    {
        _knobs[i].Init(
            hw.adc.GetPtr(i),
            hw.AudioCallbackRate()
        );
    }
}