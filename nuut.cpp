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
Voice voices[14];

// Shared state for the Comb LP6 breath modulation.
CombLP6BreathState combLP6Breath;

// Global stereo delay used for the main reverb.
constexpr size_t DELAY_SIZE = 24000;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayL;
daisysp::DelayLine<float, DELAY_SIZE> reverbDelayR;

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
constexpr int MELODY_LENGTH = 5;
float melodyNotes[MELODY_LENGTH] = {};
int melodyStep = 0;
int melodyLastAspect = ASPECT_NONE;
int melodyLastA = -1;
int melodyLastB = -1;
size_t melodySamplesUntilNext = 0;
float melodyCurrentFrequency = 432.0f;
bool melodyActive = false;
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
       // --------------------------------------------------------
       // PRIMARY CONTROLS
       // --------------------------------------------------------
       float originValue = knobs.s31().Process();
       float arcValue = knobs.s32().Process();
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
       // Build a five-note phrase when the detected aspect changes.
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
               int root = static_cast<int>(roundf(
                   69.0f + 12.0f * log2f(melodyFrequencyA / 432.0f)
               ));
               int target = static_cast<int>(roundf(
                   69.0f + 12.0f * log2f(melodyFrequencyB / 432.0f)
               ));

                ///
                switch(currentAspect)
                {
                    case ASPECT_CONJUNCTION:
                        melodyNotes[0] = MelodyMidiToFrequency(root);
                        melodyNotes[1] = MelodyMidiToFrequency(root + 2);
                        melodyNotes[2] = MelodyMidiToFrequency(target);
                        melodyNotes[3] = MelodyMidiToFrequency(root + 2);
                        melodyNotes[4] = MelodyMidiToFrequency(root);
                        break;

                    case ASPECT_SEXTILE:
                        melodyNotes[0] = MelodyMidiToFrequency(root);
                        melodyNotes[1] = MelodyMidiToFrequency(root + 2);
                        melodyNotes[2] = MelodyMidiToFrequency(target);
                        melodyNotes[3] = MelodyMidiToFrequency(root + 7);
                        melodyNotes[4] = MelodyMidiToFrequency(target);
                        break;

                    case ASPECT_SQUARE:
                        melodyNotes[0] = MelodyMidiToFrequency(root);
                        melodyNotes[1] = MelodyMidiToFrequency(target);
                        melodyNotes[2] = MelodyMidiToFrequency(root + 6);
                        melodyNotes[3] = MelodyMidiToFrequency(root);
                        melodyNotes[4] = MelodyMidiToFrequency(root);
                        break;

                    case ASPECT_TRINE:
                        melodyNotes[0] = MelodyMidiToFrequency(root);
                        melodyNotes[1] = MelodyMidiToFrequency(root + 4);
                        melodyNotes[2] = MelodyMidiToFrequency(target);
                        melodyNotes[3] = MelodyMidiToFrequency(root + 9);
                        melodyNotes[4] = MelodyMidiToFrequency(target);
                        break;

                    case ASPECT_OPPOSITION:
                        melodyNotes[0] = MelodyMidiToFrequency(root);
                        melodyNotes[1] = MelodyMidiToFrequency(target);
                        melodyNotes[2] = MelodyMidiToFrequency(root);
                        melodyNotes[3] = MelodyMidiToFrequency(target);
                        melodyNotes[4] = MelodyMidiToFrequency(root);
                        break;

                    default:
                        break;
                }



               melodyStep = 0;
               melodyCurrentFrequency = melodyNotes[0];
               melodySamplesUntilNext = 9000;
               melodyActive = true;
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
               // Trigger the voice only when it becomes active.
               if(!constellationVoiceWasActive[idx])
               {
                   constellationVoices[idx].triggered = true;
               }
           }
       }

       // --------------------------------------------------------
       // SOUND CONTROLS
       // --------------------------------------------------------

       float originSemitones = originValue * 24.0f - 12.0f;
       float originRatio = powf(2.0f, originSemitones / 12.0f);
       float Alchemy = knobs.s33().Process();

       // Crystal effect controls.
       float crystalRefraction = knobs.s34().Process();
       float crystalRadiance = knobs.s37().Process();
       float crystalWet = knobs.s35().Process();

       // --------------------------------------------------------
       // AUDIO PROCESSING
       // Generate the planetary voices and constellation voices
       // for every sample in the current audio block.
       // --------------------------------------------------------

       for(size_t i = 0; i < size; i++)
       {
           float planetSig = 0.0f;
           float constellationSig = 0.0f;
           // Advance the melody at roughly 0.1875-second intervals at 48 kHz.
           if(melodyActive && AnyPadActive())
           {
               if(melodySamplesUntilNext > 0)
                   --melodySamplesUntilNext;
               if(melodySamplesUntilNext == 0)
               {
                   melodyStep = (melodyStep + 1) % MELODY_LENGTH;
                   melodyCurrentFrequency = melodyNotes[melodyStep];
                   melodySamplesUntilNext = 9000;
               }
           }
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
               if(idx == 6 && melodyActive && AnyPadActive())
               {
                   // The seventh extra voice temporarily plays the melody.
                   constellationVoices[idx].active = true;
                   constellationVoices[idx].targetFrequency =
                       melodyCurrentFrequency * originRatio;
               }
               else if(planet != -1 &&
                       constellation.IsExtraVoice(v) &&
                       AnyPadActive())
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
                   gate = padStates[planet];
               planetSig += ProcessVoice(
                   &voices[v],
                   gate,
                   0.0f,
                   &aspect,
                   originRatio,
                   arcValue,
                   Alchemy,
                   &combLP6Breath
               );
           }
           // ----------------------------------------------------
           // VOICE MIX
           // Combine the main and constellation signals.
           // ----------------------------------------------------
           float sig = planetSig + constellationSig * 1.10f;
           // ----------------------------------------------------
           // GLOBAL REVERB
           // Cross-feedback between the two delay lines creates
           // the existing stereo reverb effect.
           // ----------------------------------------------------
           float reverbL = reverbDelayL.Read();
           float reverbR = reverbDelayR.Read();
           reverbDelayL.Write(
               sig + reverbR * 0.35f
           );
           reverbDelayR.Write(
               sig + reverbL * 0.35f
           );
           sig =
               sig * 0.75f
               + reverbL * 0.15f
               + reverbR * 0.15f;
           // Preserve the post-reverb signal for the dry/wet mix.
           float drySig = sig;
           // ----------------------------------------------------
           // CRYSTAL EFFECTS
           // Process the signal using the selected crystal mode.
           // ----------------------------------------------------
           CrystalStereo crystalSig = crystals.Process(
               sig,
               switches.A(),
               crystalRefraction,
               crystalRadiance,
               AnyPadActive()
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
}
// ============================================================
// MAIN
// ============================================================
int main()
{
       hw.Configure();
       hw.Init();
       knobs.Init(hw);
       hw.SetLed(true);
       System::Delay(1000);
       hw.SetLed(false);
       pads.Init();
       switches.Init();
       transit.Init();
       float sampleRate = hw.AudioSampleRate();
       InitNUUTSineWavetable();
       InitCombLP6Breath(&combLP6Breath, sampleRate);
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
