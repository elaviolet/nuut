#pragma once

#include "daisy_seed.h"

class Switches {
public:
    Switches() {};
    ~Switches() {};

    void Init();

    int A();
    int B();

private:
    daisy::Switch3 _switch_7_8;
    daisy::Switch3 _switch_9_10;
};
