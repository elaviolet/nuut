#pragma once

#include "planets.h"
#include "aspects/aspects_data.h"

#include <cmath>

// ============================================================
// PLANET ORBIT
// Stores the current angular position and movement speed
// of a planet in the transit system.
// ============================================================

struct PlanetOrbit
{
    float position = 0.0f;
    float speed = 1.0f;
};

// ============================================================
// TRANSIT
// Calculates planetary positions and detects angular aspects.
//
// This class handles the astronomical side of the system.
// Aspect-specific sound behaviour is handled separately.
// ============================================================

class Transit
{
public:

    // --------------------------------------------------------
    // INITIALIZATION
    // Loads the starting positions defined in planets.h
    // and assigns each planet its movement speed.
    // --------------------------------------------------------

    void Init()
    {
        for(int i = 0; i < 10; i++)
        {
            planets[i].position = initialPlanetPositions[i];
            // Real speeds from planets.h (the web wheel uses the same ones).
            planets[i].speed = ::planets[i].movementSpeed;
        }
    }

    // --------------------------------------------------------
    // SET TRANSIT POSITION
    // Maps the normalized input value to a 360-degree cycle.
    // Each planet moves according to its individual speed.
    // --------------------------------------------------------

    void SetPosition(double value)
    {
        constexpr float kDeg = 0.01745329252f;
        const double turn = value * 360.0;                    // time in degrees of the Earth's orbit (one cycle = one year)
        const float earth = (float)std::fmod(earthStartDeg + turn, 360.0) * kDeg;

        for(int i = 0; i < 10; i++)
        {
            float pos;

            if(i == 2)           // Sun: opposite to the Earth's heliocentric position
                pos = (float)std::fmod(earthStartDeg + 180.0 + turn, 360.0);
            else if(i == 9)      // Moon: turns around the Earth
                pos = (float)std::fmod(initialPlanetPositions[i] + turn * planets[i].speed, 360.0);
            else                 // planet: direction seen from the Earth
            {
                // Reduced in double precision: with a long time the angle gets large.
                const float a = (float)std::fmod(orbits[i].startDeg + turn * planets[i].speed, 360.0) * kDeg;
                pos = atan2f(orbits[i].radius * sinf(a) - sinf(earth),
                             orbits[i].radius * cosf(a) - cosf(earth)) / kDeg;
            }

            // Keep the angular position within 0-360 degrees.
            pos = fmodf(pos, 360.0f);
            if(pos < 0.0f)
                pos += 360.0f;
            planets[i].position = pos;
        }
    }

    // --------------------------------------------------------
    // GET PLANET POSITION
    // Returns the current angular position of a planet.
    // Returns -1 if the index is invalid.
    // --------------------------------------------------------

    float GetPosition(int planet) const
    {
        if(planet < 0 || planet >= 10)
            return -1.0f;

        return planets[planet].position;
    }

    // --------------------------------------------------------
    // ANGULAR DISTANCE
    // Calculates the shortest angular distance between
    // two positions, accounting for the 0/360-degree boundary.
    // Result: 0–180 degrees.
    // --------------------------------------------------------

    float GetAngularDistance(float a, float b) const
    {
        float distance = fabsf(a - b);

        if(distance > 180.0f)
            distance = 360.0f - distance;

        return distance;
    }

    // --------------------------------------------------------
    // DETECT ASPECT
    // Identifies the closest supported aspect angle within
    // the current orb of 7 degrees.
    //
    // Returns the corresponding AspectType value,
    // or ASPECT_NONE when no aspect is detected.
    // --------------------------------------------------------

    int DetectAspect(float distance) const
    {
        const float orb = 7.0f;

        const float aspectAngles[5] =
        {
            0.0f,    // Conjunction
            60.0f,   // Sextile
            90.0f,   // Square
            120.0f,  // Trine
            180.0f   // Opposition
        };

        int bestAspect = ASPECT_NONE;
        float bestError = orb + 1.0f;

        for(int i = 0; i < 5; i++)
        {
            float error = fabsf(distance - aspectAngles[i]);

            if(error <= orb && error < bestError)
            {
                bestAspect = i;
                bestError = error;
            }
        }

        return bestAspect;
    }

    // --------------------------------------------------------
    // ASPECT PROXIMITY
    // Converts the distance from the exact aspect angle into
    // a normalized value:
    //   1.0 = exact aspect
    //   0.0 = outside the 7-degree orb
    // --------------------------------------------------------

    float GetAspectProximity(float distance, int aspect) const
    {
        if(aspect < 0 || aspect >= 5)
            return 0.0f;

        const float orb = 7.0f;

        const float aspectAngles[5] =
        {
            0.0f,    // Conjunction
            60.0f,   // Sextile
            90.0f,   // Square
            120.0f,  // Trine
            180.0f   // Opposition
        };

        float error = fabsf(distance - aspectAngles[aspect]);

        if(error >= orb)
            return 0.0f;

        return 1.0f - (error / orb);
    }

    // --------------------------------------------------------
    // CHECK ASPECT
    // Checks whether two specified planets form an aspect.
    // Returns the aspect type, or ASPECT_NONE if invalid
    // or if no aspect is detected.
    // --------------------------------------------------------

    int CheckAspect(int planetA, int planetB) const
    {
        if(planetA < 0 || planetA >= 10 ||
           planetB < 0 || planetB >= 10)
        {
            return ASPECT_NONE;
        }

        float distance = GetAngularDistance(
            planets[planetA].position,
            planets[planetB].position
        );

        return DetectAspect(distance);
    }

    // --------------------------------------------------------
    // FIND ASPECT
    // Searches the unique pairs of the allowed planets and returns
    // the closest detected aspect.
    //
    // Outputs:
    //   planetA / planetB = indices of the planets involved
    //   distance          = their angular distance
    //
    // Returns ASPECT_NONE if no aspect is found.
    // --------------------------------------------------------

    int FindAspect(int& planetA,
                   int& planetB,
                   float& distance,
                   unsigned planetMask = 0x3FFu) const
    {
        // Only planets whose bit is set in planetMask are considered
        // (bit n = planet n). Among all pairs the CLOSEST aspect wins,
        // so the result does not depend on the order of the planets.
        const float aspectAngles[5] = {0.0f, 60.0f, 90.0f, 120.0f, 180.0f};

        planetA = -1;
        planetB = -1;
        distance = 0.0f;

        int bestAspect = ASPECT_NONE;
        float bestError = 1000.0f;

        for(int i = 0; i < 10; i++)
        {
            if(!((planetMask >> i) & 1u))
                continue;

            for(int j = i + 1; j < 10; j++)
            {
                if(!((planetMask >> j) & 1u))
                    continue;

                const float angularDistance = GetAngularDistance(
                    planets[i].position,
                    planets[j].position
                );

                const int aspect = DetectAspect(angularDistance);

                if(aspect == ASPECT_NONE)
                    continue;

                const float error
                    = fabsf(angularDistance - aspectAngles[aspect]);

                if(error < bestError)
                {
                    bestError = error;
                    bestAspect = aspect;
                    planetA = i;
                    planetB = j;
                    distance = angularDistance;
                }
            }
        }

        return bestAspect;
    }

    // --------------------------------------------------------
    // LINGER FACTOR
    // 1 = the sky runs freely, about 0.06 = almost stopped. It slows down
    // when two of the planets in planetMask come near an aspect angle, so
    // that the event lasts longer.
    // --------------------------------------------------------
    float LingerFactor(unsigned planetMask) const
    {
        const float aspectAngles[5] = {0.0f, 60.0f, 90.0f, 120.0f, 180.0f};
        float best = 99.0f;

        for(int i = 0; i < 10; i++)
        {
            if(!((planetMask >> i) & 1u))
                continue;
            for(int j = i + 1; j < 10; j++)
            {
                if(!((planetMask >> j) & 1u))
                    continue;
                const float d = GetAngularDistance(planets[i].position,
                                                   planets[j].position);
                for(int k = 0; k < 5; k++)
                {
                    const float e = fabsf(d - aspectAngles[k]);
                    if(e < best)
                        best = e;
                }
            }
        }

        float x = (best - 2.0f) / 8.0f;
        x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        return 0.06f + 0.94f * x * x * (3.0f - 2.0f * x);
    }

private:

    // Current state of the ten planetary orbits.
    PlanetOrbit planets[10];
};