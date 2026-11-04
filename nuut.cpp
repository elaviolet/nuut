#include "daisy_seed.h"
#include <cmath>
#include "daisysp.h"
#include "Utility/delayline.h"
#include "touch/pads.h"
#include "touch/knobs.h"
#include "touch/switches.h"
#include "nuut/voice.h"
#include "nuut/planets.h"
#include "nuut/constellation.h"
#include "nuut/aspects/aspects_engine.h"
#include "nuut/crystals.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;
CpuLoadMeter cpuMeter;

Voice voices[14];

NUUTEventVoice eventVoice;
bool eventBellTriggerPending = false;
float eventBellFrequency = 432.0f;

// Shared state for the Comb LP6 breath modulation.
CombLP6BreathState combLP6Breath;

// Global stereo delay used for the main reverb.
DSY_SDRAM_BSS daisysp::ReverbSc reverb;

Pads pads;
Knobs knobs;
Switches switches;
Constellation constellation;
Transit transit;
Crystals crystals;

// ============================================================
// GLOBAL CONTROL STATE
// ============================================================
// Tracks which of the ten planetary pads are currently active.

bool padStates[10] = {false};

// When Hold is enabled, a selected planet remains active
// after its touch pad is released.

bool holdEnabled = false;
float transitPosition = 0.0f;

constexpr int MELODY_LENGTH = 3;
float melodyNotes[MELODY_LENGTH] = {};

int melodyStep = 0;
int melodyLength = 3;

int melodyLastAspect = ASPECT_NONE;
int melodyLastA = -1;
int melodyLastB = -1;

size_t melodySamplesUntilNext = 0;
float melodyCurrentFrequency = 432.0f;
bool melodyActive = false;
int melodyTrillTogglesRemaining = 0;
bool melodyTrillHigh = false;
float melodyTrillBaseFrequency = 432.0f;

bool AnyPadActive();

// ============================================================
// PAD TOUCH CALLBACK
// Handles planet selection and Hold mode.
// ============================================================

void OnPadTouch(uint16_t pad)
{
       // Pad 11 toggles Hold mode.
       if(pad == 11)
       {
           holdEnabled = !holdEnabled;
           if(holdEnabled)
           {
               // Indicate that Hold mode is enabled.
               hw.SetLed(true);
           }
           else
           {
               // Disabling Hold releases every selected planet.
               for(uint16_t i = 0; i < 10; i++)
               {
                   padStates[i] = false;
                   constellation.ReleasePlanet(i);
               }
               hw.SetLed(false);
           }
return;
       }
       // Pads 0–9 select planets.
       if(pad < 10)
       {
           if(holdEnabled)
           {
               // In Hold mode, touching a new pad replaces
               // the previously selected planet.
               for(uint16_t i = 0; i < 10; i++)
               {
                   padStates[i] = false;
                   constellation.ReleasePlanet(i);
               }
           }
           padStates[pad] = true;
           hw.SetLed(true);
           // Update the main voices and their constellation.
           constellation.AssignPlanet(pad);
       }
}

// ============================================================
// PAD RELEASE CALLBACK
// Releases a planet unless Hold mode is active.
// ============================================================

void OnPadRelease(uint16_t pad)
{
       if(pad < 10)
       {
           if(holdEnabled)
           {
               // Hold mode keeps the selected planet active.
return;
           }
           padStates[pad] = false;
           constellation.ReleasePlanet(pad);
           // The LED remains on while any planet is active.
           hw.SetLed(AnyPadActive());
       }
}

// ============================================================
// ACTIVE PAD CHECK
// Returns true if at least one planetary pad is active.
// ============================================================

bool AnyPadActive()
{
       for(uint16_t i = 0; i < 10; i++)
       {
           if(padStates[i])
return true;
       }
return false;
}
float MelodyMidiToFrequency(int midiNote)
{
return 432.0f * powf(
           2.0f,
           (static_cast<float>(midiNote) - 69.0f) / 12.0f
       );
}

// ============================================================
// AUDIO CALLBACK
// Processes controls, voices, transit aspects, reverb,
// crystal effects and stereo output.
// ============================================================

void AudioCallback(
       AudioHandle::InputBuffer in,
       AudioHandle::OutputBuffer out,
       size_t size)
{
       cpuMeter.OnBlockStart();
       // --------------------------------------------------------
       // PRIMARY CONTROLS
       // --------------------------------------------------------
       float originValue = knobs.s31().Process();
       float arcValue = knobs.s33().Process();
       // --------------------------------------------------------
       // TRANSIT CONTROL
       // The center switch position provides manual control.
       // The other positions enable automatic orbital movement.
       // --------------------------------------------------------
       float transitKnob = knobs.s36().Process();
       int transitMode = switches.B();
       if(transitMode == Switch3::POS_CENTER)
       {
           // Manual mode: the knob directly sets transit position.
           transitPosition = transitKnob;
       }
       else
       {
           // Automatic mode starts from the current position.
           float speed = 0.00005f;
           if(transitMode == Switch3::POS_DOWN)
               speed = 0.00015f;
           transitPosition += speed;
           // Wrap around when the normalized cycle is complete.
           if(transitPosition > 1.0f)
               transitPosition -= 1.0f;
       }
       transit.SetPosition(transitPosition);

       // --------------------------------------------------------
       // CONSTELLATION CONTROL
       // --------------------------------------------------------

       float constellationValue = knobs.s30().Process();
       constellation.SetConstellation(constellationValue);

       // --------------------------------------------------------
       // ASPECT DETECTION
       // Transit finds the first active planetary aspect.
       // The aspect engine converts it into sound parameters.
       // --------------------------------------------------------

       int aspectA = -1;
       int aspectB = -1;
       float aspectDistance = 0.0f;
       int currentAspect = transit.FindAspect(
           aspectA,
           aspectB,
           aspectDistance
       );
       float aspectProximity = 0.0f;
       if(currentAspect != ASPECT_NONE)
       {
           aspectProximity = transit.GetAspectProximity(
               aspectDistance,
               currentAspect
           );
       }
       AspectData aspect = BuildAspectData(
           currentAspect,
           aspectProximity
       );

      
        // --------------------------------------------------------
        // MELODY GENERATION
        // Build a note phrase when the detected aspect changes.
        // --------------------------------------------------------

        if(currentAspect != ASPECT_NONE && aspectA >= 0 && aspectB >= 0)
        {
            float melodyFrequencyA = planets[aspectA].frequency;
            float melodyFrequencyB = planets[aspectB].frequency;

            // Keep both planetary pitches in a useful register.
            while(melodyFrequencyA < 220.0f)
                melodyFrequencyA *= 2.0f;
            while(melodyFrequencyA > 880.0f)
                melodyFrequencyA *= 0.5f;

            while(melodyFrequencyB < 220.0f)
                melodyFrequencyB *= 2.0f;
            while(melodyFrequencyB > 880.0f)
                melodyFrequencyB *= 0.5f;

            if(!melodyActive ||
            currentAspect != melodyLastAspect ||
            aspectA != melodyLastA ||
            aspectB != melodyLastB)
            {
                // Build a three-note arpeggio using the generator planet as root.
                    // Preserve the chord intervals when transposing to the planet frequency.
                    const float rootFrequency = melodyFrequencyA;

                    switch(currentAspect)
                    {
                        case ASPECT_CONJUNCTION: // G-C-E: 0, +5, +9
                            melodyNotes[0] = rootFrequency;
                            melodyNotes[1] = rootFrequency * powf(2.0f, 5.0f / 12.0f);
                            melodyNotes[2] = rootFrequency * powf(2.0f, 9.0f / 12.0f);
                            melodyLength = 3;
                            break;

                        case ASPECT_SEXTILE: // G-C-F: 0, +5, +10
                            melodyNotes[0] = rootFrequency;
                            melodyNotes[1] = rootFrequency * powf(2.0f, 5.0f / 12.0f);
                            melodyNotes[2] = rootFrequency * powf(2.0f, 10.0f / 12.0f);
                            melodyLength = 3;
                            break;

                        case ASPECT_TRINE: // A-C#-E: 0, +4, +7
                            melodyNotes[0] = rootFrequency;
                            melodyNotes[1] = rootFrequency * powf(2.0f, 4.0f / 12.0f);
                            melodyNotes[2] = rootFrequency * powf(2.0f, 7.0f / 12.0f);
                            melodyLength = 3;
                            break;

                        case ASPECT_OPPOSITION: // Bb-C#-E: 0, +4, +7
                            melodyNotes[0] = rootFrequency;
                            melodyNotes[1] = rootFrequency * powf(2.0f, 4.0f / 12.0f);
                            melodyNotes[2] = rootFrequency * powf(2.0f, 7.0f / 12.0f);
                            melodyLength = 3;
                            break;

                        case ASPECT_SQUARE: // A-C#-F: 0, +4, +9
                            melodyNotes[0] = rootFrequency;
                            melodyNotes[1] = rootFrequency * powf(2.0f, 4.0f / 12.0f);
                            melodyNotes[2] = rootFrequency * powf(2.0f, 9.0f / 12.0f);
                            melodyLength = 3;
                            break;

                        default:
                            melodyLength = 0;
                            break;
                    
                }

                if(melodyLength > 0)
                {
                    // Start with the first chord note active.
                    melodyStep = 1;
                    melodyCurrentFrequency = melodyNotes[0];
                    melodySamplesUntilNext = 24000;

                    // Select the event timbre according to the detected aspect.
                    switch (currentAspect)
                    {
                        case ASPECT_SQUARE:
                            SetNUUTEventTimbre(
                                &eventVoice,
                                NUUTEventTimbre::TribalDrum);
                            break;

                        case ASPECT_OPPOSITION:
                            SetNUUTEventTimbre(
                                &eventVoice,
                                NUUTEventTimbre::Gong);

                            // One Gong strike: use only the first note.
                            eventBellFrequency = melodyNotes[0];
                            break;

                        case ASPECT_CONJUNCTION:
                            SetNUUTEventTimbre(
                                &eventVoice,
                                NUUTEventTimbre::CrystalBell);
                            break;

                        case ASPECT_SEXTILE:
                            SetNUUTEventTimbre(
                                &eventVoice,
                                NUUTEventTimbre::String);
                            break;

                        case ASPECT_TRINE:
                            SetNUUTEventTimbre(
                                &eventVoice,
                                NUUTEventTimbre::Flute);
                            break;

                        default:
                            break;
                    }

                    // Trigger the bell only when a planet is active.
                    if(AnyPadActive())
                        eventBellTriggerPending = true;

                    melodyActive = true;
                }
                else
                {
                    melodyActive = false;
                }

                melodyLastAspect = currentAspect;
                melodyLastA = aspectA;
                melodyLastB = aspectB;
            }
        }
        else
        {
            melodyActive = false;
            melodyLastAspect = ASPECT_NONE;
            melodyLastA = -1;
            melodyLastB = -1;
        }

       // --------------------------------------------------------
       // CONSTELLATION VOICE TRIGGERS
       // Detect newly activated extra voices so their trigger
       // envelopes can be started when they enter the constellation.
       // --------------------------------------------------------
       bool currentConstellationActive[7] = {false};
       for(int v = 3; v < 10; v++)
       {
           int idx = v - 3;
           int planet = constellation.GetPlanet(v);
           if(planet != -1 && constellation.IsExtraVoice(v))
           {
               currentConstellationActive[idx] = true;
              
           }
       }

       // --------------------------------------------------------
       // SOUND CONTROLS
       // --------------------------------------------------------

       float originSemitones = originValue * 24.0f - 12.0f;
       float originRatio = powf(2.0f, originSemitones / 12.0f);

       // ALCHEMY
       float Alchemy = knobs.s37().Process();

       // Crystal effect controls.
       float crystalRefraction = knobs.s34().Process();
       float crystalRadiance = crystalRefraction;
       float crystalWet = knobs.s35().Process();

       // KOSMOS
       float kosmos = knobs.s32().Process();

       // --------------------------------------------------------
       // AUDIO PROCESSING
       // Generate the planetary voices and constellation voices
       // for every sample in the current audio block.
       // --------------------------------------------------------

       const bool anyPadActive = AnyPadActive();

        // EVENT MODULATION
        static float eventModPhase = 0.0f;

        float eventModRate = 0.0f;
        float eventModDepth = 0.0f;

        if(anyPadActive)
        {
            switch(currentAspect)
            {
                case ASPECT_CONJUNCTION:
                    eventModRate = 0.18f;
                    eventModDepth = 0.07f;
                    break;

                case ASPECT_SEXTILE:
                    eventModRate = 0.45f;
                    eventModDepth = 0.04f;
                    break;

                case ASPECT_SQUARE:
                    eventModRate = 2.0f;
                    eventModDepth = 0.10f;
                    break;

                case ASPECT_TRINE:
                    eventModRate = 0.12f;
                    eventModDepth = 0.05f;
                    break;

                case ASPECT_OPPOSITION:
                    eventModRate = 0.65f;
                    eventModDepth = 0.08f;
                    break;

                default:
                    break;
            }
        }

        const float eventModSampleRate = hw.AudioSampleRate();

       for(size_t i = 0; i < size; i++)
       {
           float planetSig = 0.0f;
           float constellationSig = 0.0f;

            // Crystal event bell and Oom: same aspect-driven trigger.
            bool eventTrigger = eventBellTriggerPending && anyPadActive;

           
            // Gradually add chord notes every 0.5 seconds.
            // Previously activated notes remain active.

            int stringNoteIndex = -1;

            if(melodyActive && anyPadActive && melodyLength > 0)
            {
                if(melodyStep < melodyLength)
                {
                    if(melodySamplesUntilNext > 0)
                        --melodySamplesUntilNext;

                    if(melodySamplesUntilNext == 0)
                        {
                            ++melodyStep;

                            if(melodyStep < melodyLength)
                                stringNoteIndex = melodyStep - 1;

                            melodySamplesUntilNext = 24000;
                        }
                }
            }

           if(eventTrigger && eventVoice.timbre == NUUTEventTimbre::String)
                stringNoteIndex = 0;

            bool stringTrigger = stringNoteIndex >= 0;

            float eventSig = ProcessNUUTEventVoice(
                &eventVoice,
                eventVoice.timbre == NUUTEventTimbre::String
                    ? stringTrigger
                    : eventTrigger,
                eventBellFrequency,
                stringNoteIndex);

            eventBellTriggerPending = false;
            
            
           for(int v = 0; v < 10; v++)
           {
               int planet = constellation.GetPlanet(v);
               // ------------------------------------------------
               // EXTRA CONSTELLATION VOICES
               // Voice indices 3–9 use the dedicated constellation
               // voice engine rather than the main voice engine.
               // ------------------------------------------------
               if(v >= 3)
               {
                   int idx = v - 3;
               // Extra voices 4, 5 and 6 temporarily form the chord pad.
                if(idx >= 4 && idx <= 6 &&
                    (currentAspect == ASPECT_OPPOSITION || currentAspect == ASPECT_SEXTILE ))
                    {
                        constellationVoices[idx].active = false;
                    }
                    else if(idx >= 4 && idx <= 6 &&
                            melodyActive && anyPadActive)
                    {
                        int noteIndex = idx - 4;

                        // Activate notes one by one, then keep them together.
                        constellationVoices[idx].active =
                            (melodyStep > noteIndex);

                        constellationVoices[idx].targetFrequency =
                            melodyNotes[noteIndex] * originRatio;
                    }
                else if(planet != -1 &&
                        constellation.IsExtraVoice(v) &&
                        anyPadActive)
                {
                    constellationVoices[idx].active = true;
                    constellationVoices[idx].targetFrequency =
                        planets[planet].frequency * originRatio;
                }
                else
                {
                    constellationVoices[idx].active = false;
                }
              
                   constellationSig += ProcessPlanetConstellationVoice(
                       &constellationVoices[idx],
                       constellationVoices[idx].targetFrequency
                   );
                   continue;
               }
               // ------------------------------------------------
               // MAIN PLANETARY VOICES
               // The gate follows the state of the assigned pad.
               // ------------------------------------------------
               bool gate = false;
               if(planet != -1)
                {
                    gate = padStates[planet];
                }
                   planetSig += ProcessVoice(
                   &voices[v],
                   gate,
                   0.0f,
                   &aspect,
                   originRatio,
                   arcValue,
                   Alchemy,
                   kosmos, 
                   v, 
                   voices[0].frequency,
                   &combLP6Breath
               );
           }
           // ----------------------------------------------------
           // VOICE MIX
           // Combine the main and constellation signals.
           // ----------------------------------------------------
           float sig = planetSig
                + constellationSig * 1.10f
                + eventSig * 0.005f;

                // Mic In
                sig += in[0][i] * 0.5f;

            // Apply the active aspect modulation.
            if(eventModDepth > 0.0f)
            {
                eventModPhase += eventModRate / eventModSampleRate;

                if(eventModPhase >= 1.0f)
                    eventModPhase -= 1.0f;

                const float eventLfo = sinf(eventModPhase * 6.2831853f);

                const float eventAmplitude =
                    1.0f + eventLfo * eventModDepth;

                sig *= eventAmplitude;
            }
                   

           // ----------------------------------------------------
            // GLOBAL STEREO REVERB — DaisySP ReverbSc
            float reverbOutL = 0.0f;
            float reverbOutR = 0.0f;

            reverb.Process(
                sig,
                sig,
                &reverbOutL,
                &reverbOutR
            );

            sig = sig * 1.05f
                + reverbOutL * 0.20f
                + reverbOutR * 0.20f; 

           // ----------------------------------------------------
           // CRYSTAL EFFECTS
           // Process the signal using the selected crystal mode.
           // ----------------------------------------------------
           float drySig = sig;

           CrystalStereo crystalSig = crystals.Process(
                sig,
                switches.A(),
                crystalRefraction,
                crystalRadiance,
                anyPadActive
            );
           // Blend the original signal with the stereo effect.
           float outputL =
               drySig * (1.0f - crystalWet)
               + crystalSig.left * crystalWet;
           float outputR =
               drySig * (1.0f - crystalWet)
               + crystalSig.right * crystalWet;
           // ----------------------------------------------------
           // OUTPUT STAGE
           // Apply the existing output gain and soft clipping.
           // ----------------------------------------------------
           float outputGain = 0.7f;
           outputL *= outputGain;
           outputR *= outputGain;
           outputL = tanhf(outputL);
           outputR = tanhf(outputR);
           out[0][i] = outputL;
           out[1][i] = outputR;
       }
       // --------------------------------------------------------
       // UPDATE CONSTELLATION ACTIVITY HISTORY
       // Store the current state for detecting new activations
       // during the next audio callback.
       // --------------------------------------------------------
       for(int i = 0; i < 7; i++)
       {
           constellationVoiceWasActive[i] =
               currentConstellationActive[i];
       }

       cpuMeter.OnBlockEnd();
}
// ============================================================
// MAIN
// ============================================================
int main()
{
       hw.Configure();
       hw.Init();

       hw.StartLog(false);
       hw.PrintLine("NUUT CPU monitor started");
       
       knobs.Init(hw);
       hw.SetLed(true);
       System::Delay(1000);
       hw.SetLed(false);
       pads.Init();
       switches.Init();
       transit.Init();
       float sampleRate = hw.AudioSampleRate();
       cpuMeter.Init(sampleRate, hw.AudioBlockSize());

      InitNUUTEventVoice(&eventVoice, sampleRate);
       InitCombLP6Breath(&combLP6Breath, sampleRate);
       const int reverbInitStatus = reverb.Init(sampleRate);

       if(reverbInitStatus != 0)
        {
            while(true)
            {
                hw.SetLed(true);
                System::Delay(100);
                hw.SetLed(false);
                System::Delay(100);
            }
        }

        reverb.SetFeedback(0.85f);
        reverb.SetLpFreq(10000.0f);

       crystals.Init(sampleRate);
       // Initialize all main voices.
       for(int i = 0; i < 14; i++)
       {
           InitVoice(&voices[i], sampleRate);
       }
       // Initialize the seven extra constellation voices.
       for(int i = 0; i < 7; i++)
       {
           InitPlanetConstellationVoice(
               constellationVoices[i],
               sampleRate
           );
       }
       // Connect the constellation engine to the voice array
       // and the planetary frequency table.
      constellation.Init(voices);
       pads.SetOnTouch(OnPadTouch);
       pads.SetOnRelease(OnPadRelease);
       hw.StartAudio(AudioCallback);
       while(true)
       {
           pads.Process();
           System::Delay(4);
       }
}