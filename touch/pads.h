#pragma once

#include "daisy_seed.h"
#include "dev/mpr121.h"

#include <stdint.h>
#include <functional>


class Pads
{
public:
    Pads() : state{0} {}
    ~Pads() {}

    bool Init();
    void Process();

    void SetOnTouch(std::function<void(uint16_t)> onTouch)
    {
        this->onTouch = onTouch;
    }

    void SetOnRelease(std::function<void(uint16_t)> onRelease)
    {
        this->onRelease = onRelease;
    }

    bool IsTouched(uint16_t pad)
    {
        return state & (1 << pad);
    }

    bool HasTouch()
    {
        return state > 0;
    }

private:
    uint16_t state;

    daisy::Mpr121I2C mpr;

    std::function<void(uint16_t)> onTouch;
    std::function<void(uint16_t)> onRelease;
};