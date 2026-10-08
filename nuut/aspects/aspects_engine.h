#pragma once

#include "aspects_data.h"

// ============================================================
// ASPECT ENGINE
// Converts a detected planetary aspect into sound parameters.
//
// Transit is responsible for detecting the aspect.
// This engine assigns the corresponding sound characteristics.
// ============================================================

inline AspectData BuildAspectData(int currentAspect, float proximity)
{
    // Start with every sound parameter at zero.
    AspectData aspect{};

    aspect.type = currentAspect;

    // Assign the existing sound parameters for each aspect.
    // These values are preserved from the original nuut.cpp.
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

        case ASPECT_NONE:
        default:
            // No detected aspect: keep all parameters at zero.
            aspect.type = ASPECT_NONE;
            return aspect;
    }

    // Proximity is meaningful only when an aspect is active.
    aspect.proximity = proximity;

    return aspect;
}