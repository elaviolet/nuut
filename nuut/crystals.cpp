#include "crystals.h"

void Crystals::Init(float sampleRate)
{
    amethystDelay.Init();
    amethystDelay.SetDelay(sampleRate * 0.35f);
    amethystShifter.Init(sampleRate);
    amethystShifter.SetTransposition(12.0f);   
}

float Crystals::Process(float input, int mode)
{
    if(mode == daisy::Switch3::POS_CENTER)
    {
        // Ametista
        float shimmer = amethystShifter.Process(input);
        float delayed = amethystDelay.Read();

        amethystDelay.Write(input + delayed * 0.45f);

        return input + shimmer * 0.35f + delayed * 0.25f;
    }

    return input;
}