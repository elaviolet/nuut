#pragma once

struct PlanetData
{
    const char* name;
    float frequency;      // Audible frequency by Cousto (Hz)
    float tuningPitch;    // Cousto tuning pitch (Hz). Not used by the code. Sun and Moon: 0.0 = value not set yet
    float movementSpeed;  // Turns per transit cycle, taken as 1 cycle = 1 Earth year
};

// Geocentric order: the Earth is the centre, so the Sun and the Moon
// take the places of "Earth" and "Day" (same indices as before).
//
// movementSpeed = real orbital speed in turns per year:
//   Moon     27.32 days sidereal      -> 13.37
//   Sun      1 year                   ->  1.00
//   Mars     1.881 years              ->  0.532
//   Jupiter  11.86 years              ->  0.0843
//   Saturn   29.46 years              ->  0.0339
//   Uranus   84.01 years              ->  0.0119
//   Neptune  164.8 years              ->  0.00607
//   Pluto    248.1 years              ->  0.00403
//
// Mercury and Venus: seen from the Earth their average motion is the same
// as the Sun's (they never leave it by more than 28 and 47 degrees), so
// with the real geocentric value they would never form new aspects with
// the Sun. Their own orbital speeds are used instead (4.15 and 1.63 turns
// per year), a deliberate compromise.
const PlanetData planets[10] =
{
    {"Mercury", 141.27f, 423.34f,  4.15f},
    {"Venus",   221.23f, 442.46f,  1.63f},
    {"Sun",     126.22f, 0.0f,  1.00f},    
    {"Mars",    144.72f, 433.67f,  0.532f},
    {"Jupiter", 183.58f, 436.62f,  0.0843f},
    {"Saturn",  147.85f, 443.04f,  0.0339f},
    {"Uranus",  207.36f, 439.37f,  0.0119f},
    {"Neptune", 211.44f, 422.87f,  0.00607f},
    {"Pluto",   140.25f, 445.26f,  0.00403f},
    {"Moon",    210.42f, 0.0f, 13.37f}     // (synodic Moon, Cousto)
};

// Speeds compressed with a square root, as a softer alternative:
// the order stays the same but the outer planets still move.
// To use them, copy these values into movementSpeed above.
//   Mercury 2.04, Venus 1.28, Sun 1.00, Mars 0.73, Jupiter 0.29,
//   Saturn 0.18, Uranus 0.11, Neptune 0.08, Pluto 0.06, Moon 3.66

const float initialPlanetPositions[10] =
{
    0.0f, 35.0f, 70.0f, 115.0f, 160.0f,
    210.0f, 250.0f, 290.0f, 330.0f, 15.0f
};