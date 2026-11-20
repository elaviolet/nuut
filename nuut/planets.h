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
// The sky is seen from the Earth (geocentric). Each planet moves on a circular
// orbit around the Sun and its position is the direction in which it is seen
// from the Earth, so the planets show retrograde motion and Mercury and Venus
// stay close to the Sun, as in an astrological chart. The Earth itself is not
// one of the sounding planets. movementSpeed is the orbital speed of the
// planet around the Sun, in turns per year (Mercury 4.15, Venus 1.63). The
// Sun is the Earth's apparent position, one turn per year. The Moon turns
// around the Earth. One transit cycle is one year.
const PlanetData planets[10] =
{
    {"Mercury", 141.27f, 423.34f,  4.15f},
    {"Venus",   221.23f, 442.46f,  1.63f},
    {"Sun",     126.22f, 0.0f,  1.00f},    // was Earth
    {"Mars",    144.72f, 433.67f,  0.532f},
    {"Jupiter", 183.58f, 436.62f,  0.0843f},
    {"Saturn",  147.85f, 443.04f,  0.0339f},
    {"Uranus",  207.36f, 439.37f,  0.0119f},
    {"Neptune", 211.44f, 422.87f,  0.00607f},
    {"Pluto",   140.25f, 445.26f,  0.00403f},
    {"Moon",    210.42f, 0.0f, 13.37f}     // was Day (synodic Moon, Cousto)
};

// Speeds compressed with a square root, as a softer alternative:
// the order stays the same but the outer planets still move.
// To use them, copy these values into movementSpeed above.
//   Mercury 2.04, Venus 1.28, Sun 1.00, Mars 0.73, Jupiter 0.29,
//   Saturn 0.18, Uranus 0.11, Neptune 0.08, Pluto 0.06, Moon 3.66

// Geocentric longitudes of the sky at the start of the cycle (spring equinox 1985).
const float initialPlanetPositions[10] =
{
    17.5f, 21.3f, 0.0f, 34.0f, 308.9f,
    238.0f, 258.0f, 273.6f, 214.2f, 350.9f
};

// Circular orbits used to compute the geocentric positions.
//   radius   = distance from the Sun in astronomical units
//   startDeg = heliocentric angle of the planet at the start of the cycle
// The start of the cycle reproduces the real sky of the spring equinox 1985
// (20 March, 16:14 UT): the angles below are fitted so that the positions seen
// from the Earth at the start match that chart. Sun and Moon are not computed
// from an orbit.
struct OrbitData
{
    float radius;
    float startDeg;
};

const float earthStartDeg = 180.0f;  // heliocentric angle of the Earth at the start

const OrbitData orbits[10] =
{
    {0.387f,  146.34f},  // Mercury
    {0.723f,  171.13f},  // Venus
    {0.0f,      0.0f},   // Sun (Earth's apparent position)
    {1.524f,   55.53f},  // Mars
    {5.203f,  300.35f},  // Jupiter
    {9.537f,  232.87f},  // Saturn
    {19.19f,  255.06f},  // Uranus
    {30.07f,  271.65f},  // Neptune
    {39.48f,  213.42f},  // Pluto
    {0.0f,      0.0f}    // Moon (turns around the Earth)
};