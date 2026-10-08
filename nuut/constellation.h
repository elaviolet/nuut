#pragma once

#include "voice.h"

#include <cmath>

// ============================================================
// CONSTELLATION VOICE STATE
// Shared state for the seven extra constellation voices.
//
// These variables are accessed by nuut.cpp to process extra
// voices and detect when a voice becomes active.
// ============================================================

PlanetConstellationVoice constellationVoices[7];

bool constellationVoiceWasActive[7] = {false};

// ============================================================
// CONSTELLATION
// Manages the relationship between selected planetary pads,
// the three main voices, and the additional constellation voices.
//
// Main voices use indices 0–2.
// Extra constellation voices use indices 3–9.
// ============================================================

class Constellation
{
public:

    // --------------------------------------------------------
    // INITIALIZATION
    // Stores references to the main voice array and the
    // planetary frequency table.
    // --------------------------------------------------------

    void Init(Voice* voiceArray, const float* frequencies)
    {
        voices = voiceArray;
        padFrequencies = frequencies;

        // Clear the initial assignments for the three main slots.
        for(int i = 0; i < 3; i++)
        {
            voicePad[i] = -1;
            mainPads[i] = -1;
            voiceIsExtra[i] = false;
        }
    }

    // --------------------------------------------------------
    // ASSIGN PLANET
    // Assigns a touched pad to the first available main slot.
    //
    // Returns false if the pad is invalid, already assigned,
    // or all three main slots are occupied.
    // --------------------------------------------------------

    bool AssignPlanet(int pad)
    {
        if(pad < 0 || pad >= 10)
            return false;

        // Do not assign the same main pad twice.
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                return false;
        }

        // Find the first available main slot.
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

    // --------------------------------------------------------
    // RELEASE PLANET
    // Removes a pad from the main selection and updates
    // the main voice assignments.
    // --------------------------------------------------------

    void ReleasePlanet(int pad)
    {
        // Remove the pad from the selected main pads.
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                mainPads[i] = -1;
        }

        // Check whether any main pads remain selected.
        bool hasMainPad = false;

        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] != -1)
            {
                hasMainPad = true;
                break;
            }
        }

        // If no main pads remain, clear the main assignments.
        if(!hasMainPad)
        {
            for(int i = 0; i < 3; i++)
            {
                voicePad[i] = -1;
                voiceIsExtra[i] = false;
            }

            return;
        }

        // Remove the released pad from any main voice slot.
        for(int i = 0; i < 3; i++)
        {
            if(voicePad[i] == pad)
            {
                voicePad[i] = -1;
                voiceIsExtra[i] = false;
            }
        }

        // Reclassify the remaining assigned voices.
        for(int i = 0; i < 3; i++)
        {
            if(voicePad[i] != -1)
                voiceIsExtra[i] = !IsMainPad(voicePad[i]);
        }
    }

    // --------------------------------------------------------
    // GET PLANET
    // Returns the pad assigned to a voice index.
    // Returns -1 if the index is invalid or no pad is assigned.
    // --------------------------------------------------------

    int GetPlanet(int voiceIndex)
    {
        if(voiceIndex < 0 || voiceIndex >= 14)
            return -1;

        return voicePad[voiceIndex];
    }

    // --------------------------------------------------------
    // CHECK EXTRA VOICE
    // Returns true if the specified voice is assigned to
    // an additional constellation pad rather than a main pad.
    // --------------------------------------------------------

    bool IsExtraVoice(int voiceIndex)
    {
        if(voiceIndex < 0 || voiceIndex >= 14)
            return false;

        return voiceIsExtra[voiceIndex];
    }

    // --------------------------------------------------------
    // SET CONSTELLATION AMOUNT
    // Controls how many additional voices are assigned.
    // Rebuilds the constellation only when the control changes
    // by more than the existing threshold.
    // --------------------------------------------------------

    void SetConstellation(float value)
    {
        constellationAmount = value;

        if(fabsf(value - lastConstellationAmount) > 0.01f)
        {
            lastConstellationAmount = value;
            RebuildConstellation();
        }
    }

private:

    // Reference to the main planetary voice array.
    Voice* voices = nullptr;

    // Frequencies associated with the ten planetary pads.
    const float* padFrequencies = nullptr;

    // Pad assigned to each of the fourteen voice slots.
    // -1 means that the slot has no assigned pad.
    int voicePad[14] =
    {
        -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1
    };

    // The three pads explicitly selected by the user.
    int mainPads[3] = {-1, -1, -1};

    // Identifies voice slots assigned to constellation extras.
    bool voiceIsExtra[14] =
    {
        false, false, false, false, false, false, false,
        false, false, false, false, false, false, false
    };

    // Current and previous constellation control values.
    float constellationAmount = 0.0f;
    float lastConstellationAmount = -1.0f;

    // --------------------------------------------------------
    // CHECK MAIN PAD
    // Returns true if the pad is one of the three selected
    // main pads.
    // --------------------------------------------------------

    bool IsMainPad(int pad)
    {
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] == pad)
                return true;
        }

        return false;
    }

    // --------------------------------------------------------
    // CHECK PAD USAGE
    // Returns true if a pad is already assigned to any
    // voice slot.
    // --------------------------------------------------------

    bool IsUsed(int pad)
    {
        for(int i = 0; i < 14; i++)
        {
            if(voicePad[i] == pad)
                return true;
        }

        return false;
    }

    // --------------------------------------------------------
    // FIND CLOSEST AVAILABLE PAD
    // Finds the unused pad whose frequency is closest to
    // the requested target frequency.
    // --------------------------------------------------------

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

    // --------------------------------------------------------
    // REBUILD CONSTELLATION
    // Recalculates the three main voice assignments and
    // assigns additional voices according to the control value.
    //
    // With one main pad, it searches for frequencies near
    // 1.5x and 2x the root frequency.
    //
    // With two main pads, it searches near their geometric mean.
    //
    // Additional voices are then assigned from unused pads.
    // --------------------------------------------------------

    void RebuildConstellation()
    {
        int desiredPads[3] = {-1, -1, -1};
        int desiredCount = 0;

        // Collect the pads explicitly selected by the user.
        for(int i = 0; i < 3; i++)
        {
            if(mainPads[i] != -1)
            {
                desiredPads[desiredCount] = mainPads[i];
                desiredCount++;
            }
        }

        // ----------------------------------------------------
        // GENERATE HARMONIC COMPANIONS
        // One selected pad can generate up to two companions.
        // ----------------------------------------------------

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

            float targetFrequency =
                std::sqrt(frequency1 * frequency2);

            int extra = FindClosestPad(targetFrequency);

            if(extra != -1)
            {
                desiredPads[desiredCount] = extra;
                desiredCount++;
            }
        }

        // ----------------------------------------------------
        // PRESERVE EXISTING VOICE ASSIGNMENTS
        // Keep pads in their current voice slots whenever
        // possible, reducing unnecessary reassignment.
        // ----------------------------------------------------

        int newVoicePad[3] = {-1, -1, -1};
        bool assigned[3] = {false, false, false};

        // Preserve assignments that are still required.
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

        // Fill empty voice slots with remaining desired pads.
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

        // Apply the updated main voice assignments.
        for(int i = 0; i < 3; i++)
        {
            bool changed = voicePad[i] != newVoicePad[i];

            voicePad[i] = newVoicePad[i];

            voiceIsExtra[i] =
                voicePad[i] != -1 && !IsMainPad(voicePad[i]);

            // Update the voice frequency only when its pad changes.
            if(changed && voicePad[i] != -1)
                ConfigureVoice(i, voicePad[i]);
        }

        // ----------------------------------------------------
        // CLEAR PREVIOUS EXTRA ASSIGNMENTS
        // Extra slots are rebuilt from the current constellation
        // amount on every rebuild.
        // ----------------------------------------------------

        for(int i = 3; i < 14; i++)
        {
            voicePad[i] = -1;
            voiceIsExtra[i] = false;
        }

        if(constellationAmount <= 0.0f)
            return;

        // Map the control value to a maximum of seven extra voices.
        int extraVoiceCount =
            static_cast<int>(constellationAmount * 7.0f);

        int extraVoiceIndex = 3;
        int addedVoices = 0;

        // Assign unused pads to extra voice slots 3–9.
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

    // --------------------------------------------------------
    // CONFIGURE VOICE
    // Updates the stored frequency for a voice using the
    // frequency associated with its assigned pad.
    // --------------------------------------------------------

    void ConfigureVoice(int voiceIndex, int pad)
    {
        if(voiceIndex < 0 || voiceIndex >= 14 ||
           pad < 0 || pad >= 10)
        {
            return;
        }

        voices[voiceIndex].frequency = padFrequencies[pad];
    }
};
