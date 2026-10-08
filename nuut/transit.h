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
            planets[i].speed = 0.55f + i * 0.075f;
        }
    }

    // --------------------------------------------------------
    // SET TRANSIT POSITION
    // Maps the normalized input value to a 360-degree cycle.
    // Each planet moves according to its individual speed.
    // --------------------------------------------------------

    void SetPosition(float value)
    {
        float transit = value * 360.0f;

        for(int i = 0; i < 10; i++)
        {
            planets[i].position =
                initialPlanetPositions[i] + transit * planets[i].speed;

            // Keep the angular position within 0–360 degrees.
            while(planets[i].position >= 360.0f)
                planets[i].position -= 360.0f;

            while(planets[i].position < 0.0f)
                planets[i].position += 360.0f;
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
    // Searches all unique planet pairs and returns the first
    // detected aspect.
    //
    // Outputs:
    //   planetA / planetB = indices of the planets involved
    //   distance          = their angular distance
    //
    // Returns ASPECT_NONE if no aspect is found.
    // --------------------------------------------------------

    int FindAspect(int& planetA, int& planetB, float& distance) const
    {
        planetA = -1;
        planetB = -1;
        distance = 0.0f;

        for(int i = 0; i < 10; i++)
        {
            for(int j = i + 1; j < 10; j++)
            {
                float angularDistance = GetAngularDistance(
                    planets[i].position,
                    planets[j].position
                );

                int aspect = DetectAspect(angularDistance);

                if(aspect != ASPECT_NONE)
                {
                    planetA = i;
                    planetB = j;
                    distance = angularDistance;

                    return aspect;
                }
            }
        }

        return ASPECT_NONE;
    }

private:

    // Current state of the ten planetary orbits.
    PlanetOrbit planets[10];
};