#pragma once

#include "voice.h"
#include <cmath>

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
        if(voiceIndex < 0 || voiceIndex >= 3)
            return -1;

        return voicePad[voiceIndex];
    }

    bool IsExtraVoice(int voiceIndex)
    {
        if(voiceIndex < 0 || voiceIndex >= 3)
            return false;

        return voiceIsExtra[voiceIndex];
    }

private:
    Voice* voices = nullptr;
    const float* padFrequencies = nullptr;

    int voicePad[3] = {-1, -1, -1};
    int mainPads[3] = {-1, -1, -1};
    bool voiceIsExtra[3] = {false, false, false};

    bool IsMainPad(int pad)
    {
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                return true;
        }

        return false;
    }

    bool IsUsed(int pad, const int* desiredPads, int count)
    {
        for(int i = 0; i < count; i++)
        {
            if(desiredPads[i] == pad)
                return true;
        }

        return false;
    }

    int FindClosestPad(float targetFrequency,
                       const int* desiredPads,
                       int count)
    {
        int bestPad = -1;
        float bestDistance = 1000000.0f;

        for(int pad = 0; pad < 10; pad++)
        {
            if(IsUsed(pad, desiredPads, count))
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

            int extra1 = FindClosestPad(
                root * 1.5f,
                desiredPads,
                desiredCount
            );

            if(extra1 != -1)
            {
                desiredPads[desiredCount] = extra1;
                desiredCount++;
            }

            int extra2 = FindClosestPad(
                root * 2.0f,
                desiredPads,
                desiredCount
            );

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

                int extra = FindClosestPad(
                    targetFrequency,
                    desiredPads,
                    desiredCount
                );

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
};