#pragma once

#include "voice.h"
#include <cmath>

struct PlanetOrbit
{
    float position = 0.0f;
    float speed = 1.0f;
};

class Constellation
{
public:
    void Init(Voice* voiceArray, const float* frequencies)
    {
        voices = voiceArray;
        padFrequencies = frequencies;

        for(int i = 0; i < 3; i++)
        {
            voicePad[i] = -1;
            mainPads[i] = -1;
            voiceIsExtra[i] = false;
        }

       for(int i = 0; i < 10; i++)
        {
            planets[i].position = initialPlanetPositions[i];
            planets[i].speed = 0.55f + i * 0.075f;
        }
    }

    bool AssignPlanet(int pad)
    {
        if(pad < 0 || pad >= 10)
            return false;

        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                return false;
        }

        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == -1)
            {
                mainPads[i] = pad;
                RebuildConstellation();
                return true;
            }
        }

        return false;
    }

    void ReleasePlanet(int pad)
    {
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                mainPads[i] = -1;
        }

        bool hasMainPad = false;

        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] != -1)
            {
                hasMainPad = true;
                break;
            }
        }

        if(!hasMainPad)
        {
            for(int i = 0; i < 3; i++)
            {
                voicePad[i] = -1;
                voiceIsExtra[i] = false;
            }

            return;
        }

        for(int i = 0; i < 3; i++)
        {
            if(voicePad[i] == pad)
            {
                voicePad[i] = -1;
                voiceIsExtra[i] = false;
            }
        }

        for(int i = 0; i < 3; i++)
        {
            if(voicePad[i] != -1)
                voiceIsExtra[i] = !IsMainPad(voicePad[i]);
        }
    }

    int GetPlanet(int voiceIndex)
    {
        if(voiceIndex < 0 || voiceIndex >= 14)
            return -1;

        return voicePad[voiceIndex];
    }

    bool IsExtraVoice(int voiceIndex)
    {
        if(voiceIndex < 0 || voiceIndex >= 14)
            return false;

        return voiceIsExtra[voiceIndex];
    }

    void SetConstellation(float value)
    {
        constellationAmount = value;

        if(fabsf(value - lastConstellationAmount) > 0.01f)
        {
            lastConstellationAmount = value;
            RebuildConstellation();
        }
    }

    void SetTransit(float value)
    {
        float transit = value * 360.0f;

        for(int i = 0; i < 10; i++)
        {
            planets[i].position =
                initialPlanetPositions[i] + transit * planets[i].speed;

            while(planets[i].position >= 360.0f)
                planets[i].position -= 360.0f;

            while(planets[i].position < 0.0f)
                planets[i].position += 360.0f;
        }
    }

    float GetAngularDistance(float a, float b)
    {
        float distance = fabsf(a - b);

        if(distance > 180.0f)
            distance = 360.0f - distance;

        return distance;
    }

     float GetPlanetPosition(int planet)
    {
        if(planet < 0 || planet >= 10)
        return -1.0f;

        return planets[planet].position;
    }

    int DetectAspect(float distance)
    {
        const float orb = 7.0f;

        const float aspectAngles[5] =
        {
            0.0f,
            60.0f,
            90.0f,
            120.0f,
            180.0f
        };

        int bestAspect = -1;
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

    float GetAspectProximity(float distance, int aspect)
    {
        if(aspect < 0 || aspect >= 5)
            return 0.0f;

        const float orb = 7.0f;

        const float aspectAngles[5] =
        {
            0.0f,
            60.0f,
            90.0f,
            120.0f,
            180.0f
        };

        float error = fabsf(distance - aspectAngles[aspect]);

        if(error >= orb)
            return 0.0f;

        return 1.0f - (error / orb);
    }

    int CheckAspect(int planetA, int planetB)
    {
        if(planetA < 0 || planetA >= 10 ||
        planetB < 0 || planetB >= 10)
            return -1;

        float distance = GetAngularDistance(
            planets[planetA].position,
            planets[planetB].position
        );

        return DetectAspect(distance);
    }

    int GetAspectPlanet(int index)
    {
        if(index < 0 || index >= 3)
            return -1;

        return mainPads[index];
    }

    int FindAspect(int& planetA, int& planetB, float& distance)
    {
        planetA = -1;
        planetB = -1;
        distance = 0.0f;

        for(int i = 0; i < 3; i++)
        {
            int a = GetAspectPlanet(i);

            if(a == -1)
                continue;

            for(int j = i + 1; j < 3; j++)
            {
                int b = GetAspectPlanet(j);

                if(b == -1)
                    continue;

                float angularDistance = GetAngularDistance(
                    planets[a].position,
                    planets[b].position
                );

                int aspect = DetectAspect(angularDistance);

                if(aspect != -1)
                {
                    planetA = a;
                    planetB = b;
                    distance = angularDistance;
                    return aspect;
                }
            }
        }

        return -1;
    }

    bool FindOpposition(int& planetA, int& planetB, float& proximity)
    {
        planetA = -1;
        planetB = -1;
        proximity = 0.0f;

        for(int i = 0; i < 3; i++)
        {
            int a = GetAspectPlanet(i);

            if(a == -1)
                continue;

            for(int j = i + 1; j < 3; j++)
            {
                int b = GetAspectPlanet(j);

                if(b == -1)
                    continue;

                float distance = GetAngularDistance(
                    planets[a].position,
                    planets[b].position
                );

                int aspect = DetectAspect(distance);

                if(aspect == 4)
                {
                    planetA = a;
                    planetB = b;
                    proximity = GetAspectProximity(distance, aspect);
                    return true;
                }
            }
        }

        return false;
    }

private:
    Voice* voices = nullptr;
    const float* padFrequencies = nullptr;

    PlanetOrbit planets[10];

    int voicePad[14] =
    {
        -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1
    };
    int mainPads[3] = {-1, -1, -1};
    bool voiceIsExtra[14] =
    {
        false, false, false, false, false, false, false,
        false, false, false, false, false, false, false
    };
    float transitMultiplier = 1.0f;
    float lastAppliedMultiplier = 1.0f;
    float constellationAmount = 0.0f;
    float lastConstellationAmount = -1.0f;

    bool IsMainPad(int pad)
    {
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                return true;
        }

        return false;
    }

    bool IsUsed(int pad)
    {
        for(int i = 0; i < 14; i++)
        {
            if(voicePad[i] == pad)
                return true;
        }

        return false;
    }

    int FindClosestPad(float targetFrequency)
    {
        int bestPad = -1;
        float bestDistance = 1000000.0f;

        for(int pad = 0; pad < 10; pad++)
        {
            if(IsUsed(pad))
                continue;

            float distance =
                std::fabs(padFrequencies[pad] - targetFrequency);

            if(distance < bestDistance)
            {
                bestDistance = distance;
                bestPad = pad;
            }
        }

        return bestPad;
    }

    void RebuildConstellation()
    {
        int desiredPads[3] = {-1, -1, -1};
        int desiredCount = 0;

        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] != -1)
            {
                desiredPads[desiredCount] = mainPads[i];
                desiredCount++;
            }
        }

        if(desiredCount == 1)
        {
            float root = padFrequencies[desiredPads[0]];

            int extra1 = FindClosestPad(root * 1.5f);

            if(extra1 != -1)
            {
                desiredPads[desiredCount] = extra1;
                desiredCount++;
            }

            int extra2 = FindClosestPad(root * 2.0f);

            if(extra2 != -1)
            {
                desiredPads[desiredCount] = extra2;
                desiredCount++;
            }
        }
        else if(desiredCount == 2)
            {
                float frequency1 = padFrequencies[desiredPads[0]];
                float frequency2 = padFrequencies[desiredPads[1]];

                float targetFrequency = std::sqrt(frequency1 * frequency2);

               int extra = FindClosestPad(targetFrequency);

                if(extra != -1)
                {
                    desiredPads[desiredCount] = extra;
                    desiredCount++;
                }
        }

        int newVoicePad[3] = {-1, -1, -1};
        bool assigned[3] = {false, false, false};

        for(int i = 0; i < 3; i++)
        {
            for(int j = 0; j < desiredCount; j++)
            {
                if(!assigned[j] && voicePad[i] == desiredPads[j])
                {
                    newVoicePad[i] = desiredPads[j];
                    assigned[j] = true;
                    break;
                }
            }
        }

        for(int i = 0; i < 3; i++)
        {
            if(newVoicePad[i] != -1)
                continue;

            for(int j = 0; j < desiredCount; j++)
            {
                if(!assigned[j])
                {
                    newVoicePad[i] = desiredPads[j];
                    assigned[j] = true;
                    break;
                }
            }
        }

        for(int i = 0; i < 3; i++)
        {
            bool changed = voicePad[i] != newVoicePad[i];

            voicePad[i] = newVoicePad[i];

            voiceIsExtra[i] =
                voicePad[i] != -1 && !IsMainPad(voicePad[i]);

            if(changed && voicePad[i] != -1)
                ConfigureVoice(i, voicePad[i]);
        }


            for(int i = 3; i < 14; i++)
            {
                voicePad[i] = -1;
                voiceIsExtra[i] = false;
            }

            if(constellationAmount <= 0.0f)
            return;

            int extraVoiceCount =
                static_cast<int>(constellationAmount * 7.0f);

            int extraVoiceIndex = 3;
            int addedVoices = 0;

            for(int pad = 0; pad < 10 && extraVoiceIndex < 10; pad++)
            {
                if(IsUsed(pad))
                    continue;

                if(addedVoices >= extraVoiceCount)
                    break;

                voicePad[extraVoiceIndex] = pad;
                voiceIsExtra[extraVoiceIndex] = true;

                ConfigureVoice(extraVoiceIndex, pad);


                extraVoiceIndex++;
                addedVoices++;
            }
    }

    void ConfigureVoice(int voiceIndex, int pad)
    {
        float baseFreq = padFrequencies[pad];

        voices[voiceIndex].frequency = baseFreq;

        if(voiceIndex == 0)
        {
            voices[voiceIndex].osc1.SetFreq(baseFreq);
            voices[voiceIndex].osc2.SetFreq(baseFreq * 2.0f);
            voices[voiceIndex].osc2BaseFrequency = baseFreq * 2.0f;
            voices[voiceIndex].osc3.SetFreq(baseFreq * 3.0f);
        }
        else if(voiceIndex == 1)
        {
            voices[voiceIndex].osc1.SetFreq(baseFreq * 1.5f);
            voices[voiceIndex].osc2.SetFreq(baseFreq * 3.0f);
            voices[voiceIndex].osc2BaseFrequency = baseFreq * 3.0f;
            voices[voiceIndex].osc3.SetFreq(baseFreq * 4.5f);
        }
        else if(voiceIndex == 2)
        {
            voices[voiceIndex].osc1.SetFreq(baseFreq * 2.0f);
            voices[voiceIndex].osc2.SetFreq(baseFreq * 4.0f);
            voices[voiceIndex].osc2BaseFrequency = baseFreq * 4.0f;
            voices[voiceIndex].osc3.SetFreq(baseFreq * 6.0f);
        }
    }

    void ConfigureVoiceWithMultiplier(
    int voiceIndex,
    int pad,
    float multiplier
    )
    {
    float baseFreq = padFrequencies[pad] * multiplier;

    voices[voiceIndex].frequency = baseFreq;

    if(voiceIndex == 0)
    {
        voices[voiceIndex].osc1.SetFreq(baseFreq);
        voices[voiceIndex].osc2.SetFreq(baseFreq * 2.0f);
        voices[voiceIndex].osc2BaseFrequency = baseFreq * 2.0f;
        voices[voiceIndex].osc3.SetFreq(baseFreq * 3.0f);
    }
    else if(voiceIndex == 1)
    {
        voices[voiceIndex].osc1.SetFreq(baseFreq * 1.5f);
        voices[voiceIndex].osc2.SetFreq(baseFreq * 3.0f);
        voices[voiceIndex].osc2BaseFrequency = baseFreq * 3.0f;
        voices[voiceIndex].osc3.SetFreq(baseFreq * 4.5f);
    }
    else if(voiceIndex == 2)
    {
        voices[voiceIndex].osc1.SetFreq(baseFreq * 2.0f);
        voices[voiceIndex].osc2.SetFreq(baseFreq * 4.0f);
        voices[voiceIndex].osc2BaseFrequency = baseFreq * 4.0f;
        voices[voiceIndex].osc3.SetFreq(baseFreq * 6.0f);
    }
    }
};