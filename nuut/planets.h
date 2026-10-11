#pragma once

struct PlanetData
{
    const char* name;
    float frequency;      // Frequenza udibile di Cousto (Hz)
    float tuningPitch;    // Tuning pitch di Cousto (Hz)
    float movementSpeed;  // Da definire
};

const PlanetData planets[10] =
{
    {"Mercury", 141.27f, 423.34f, 0.0f},
    {"Venus",   221.23f, 442.46f, 0.0f},
    {"Earth",   136.10f, 432.10f, 0.0f},
    {"Mars",    144.72f, 433.67f, 0.0f},
    {"Jupiter", 183.58f, 436.62f, 0.0f},
    {"Saturn",  147.85f, 443.04f, 0.0f},
    {"Uranus",  207.36f, 439.37f, 0.0f},
    {"Neptune", 211.44f, 422.87f, 0.0f},
    {"Pluto",   140.25f, 445.26f, 0.0f},
    {"Day",     194.18f, 435.92f, 0.0f}
};

const float initialPlanetPositions[10] =
{
    0.0f, 35.0f, 70.0f, 115.0f, 160.0f,
    210.0f, 250.0f, 290.0f, 330.0f, 15.0f
};
