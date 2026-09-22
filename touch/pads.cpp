#include "pads.h"

using namespace daisy;

bool Pads::Init()
{
    Mpr121I2C::Config config;

    return mpr.Init(config) == Mpr121I2C::OK;
}


void Pads::Process()
{
    uint16_t currentState = mpr.Touched();

    for(uint16_t i = 0; i < 12; i++)
    {
        uint16_t mask = 1 << i;

        bool touched = currentState & mask;
        bool wasTouched = state & mask;

        if(touched && !wasTouched)
        {
            if(onTouch)
                onTouch(i);
        }
        else if(!touched && wasTouched)
        {
            if(onRelease)
                onRelease(i);
        }
    }

    state = currentState;
}