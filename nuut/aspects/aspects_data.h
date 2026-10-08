#pragma once

// ============================================================
// ASPECT TYPES
// Identifies the five aspects supported by NUUT.
// Numeric values are preserved for compatibility with Transit.
// ============================================================

enum AspectType
{
    ASPECT_NONE        = -1,
    ASPECT_CONJUNCTION =  0,
    ASPECT_SEXTILE     =  1,
    ASPECT_SQUARE      =  2,
    ASPECT_TRINE       =  3,
    ASPECT_OPPOSITION  =  4
};

// ============================================================
// ASPECT DATA
// Stores the detected aspect and its parameters for the
// sound engine.
//
// All parameters are initialized to zero to avoid undefined
// values when no aspect is active.
// ============================================================

struct AspectData
{
    int type = ASPECT_NONE;

    float proximity = 0.0f;
    float intensity = 0.0f;
    float shimmer   = 0.0f;
    float tension   = 0.0f;
};