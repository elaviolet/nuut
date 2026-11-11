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
#include "nuut/events.h"
#include "nuut/crystals.h"
#include "nuut/ether.h"
#include "nuut/chord_flow.h"

using namespace daisy;
using namespace daisysp;

// Set to 0 to disable the once-per-second CPU load print over USB serial.
#define NUUT_CPU_LOG 1

// ============================================================
// CONSTANTS
// ============================================================
namespace
{
constexpr size_t kAudioBlockSize = 48;

constexpr int kNumPads         = 10;
constexpr int kNumMainVoices   = 3;
constexpr int kNumExtraVoices  = 7;
constexpr int kNumVoicesInUse  = kNumMainVoices + kNumExtraVoices;
constexpr int kNumVoiceSlots   = 14;

// Extra voices 4..6 double as the chord pad.
constexpr int kChordFirstExtra = 4;
constexpr int kChordLastExtra  = 6;

constexpr int kHoldPad = 11;

// Pad 10 is a modifier: while it is held, the Alchemy fader
// controls the waveform of the main voices instead of Alchemy.
constexpr int kShiftPad = 10;
volatile bool shiftHeld = false;

constexpr float kTwoPi = 6.2831853f;

// Transit 
constexpr float kTransitSlowPerSample = 0.00005f / 48.0f;
constexpr float kTransitFastPerSample = 0.00015f / 48.0f;

constexpr float kConstellationGain = 1.10f;
constexpr float kEventGain         = 0.008f;
constexpr float kMicGain           = 0.25f;

// Reverb
constexpr float kReverbFeedback    = 0.93f;
constexpr float kReverbLpHz        = 7000.0f;
constexpr float kReverbDryGain     = 1.0f;
constexpr float kReverbWetGain     = 0.34f;
constexpr float kOutputGain        = 0.7f;

constexpr float kMelodyNoteSeconds = 0.5f;


// Chords, instruments, modulation and the chord pad of each aspect are
// in nuut/events.h, in the table "ASPECT PROFILES" (one row per aspect).

inline float FastTanh(float x)
{
    if(x > 3.0f)
        return 1.0f;
    if(x < -3.0f)
        return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
} 

DaisySeed hw;
CpuLoadMeter cpuMeter;

Voice voices[kNumVoiceSlots];

NUUTEventVoice eventVoice;
bool eventBellTriggerPending = false;
float eventBellFrequency = 432.0f;

// Global Alchemy stage: sweeping filter, slow comb and stereo echo.
EtherealStage ethereal;

// Global stereo delay used for the main reverb.
DSY_SDRAM_BSS daisysp::ReverbSc reverb;

Pads pads;
Knobs knobs;
Switches switches;
Constellation constellation;
Transit transit;
Crystals crystals;
ChordFlow chordFlow;

// ============================================================
// GLOBAL CONTROL STATE
// ============================================================
// Written from the main loop (touch callbacks) and read from the
// audio interrupt, hence volatile.

volatile bool padStates[kNumPads] = {false};

// Order in which the pads were touched (used in Hold: a new pad
// replaces the oldest one when all the main voices are taken).
volatile uint32_t padOrder[kNumPads] = {0};
uint32_t padOrderCounter = 0;

// When Hold is enabled, a selected planet remains active
// after its touch pad is released.
volatile bool holdEnabled = false;

float transitPosition = 0.0f;

constexpr int MELODY_LENGTH = 3;
float melodyNotes[MELODY_LENGTH] = {};

int melodyStep   = 0;
int melodyLength = 3;

int melodyLastAspect = ASPECT_NONE;
int melodyLastA      = -1;
int melodyLastB      = -1;

size_t melodyNoteIntervalSamples = 24000; // set from the sample rate in main()
size_t melodySamplesUntilNext    = 0;
float melodyCurrentFrequency     = 432.0f;
bool melodyActive                = false;

bool AnyPadActive();

// ============================================================
// PAD TOUCH CALLBACK
// ============================================================

void OnPadTouch(uint16_t pad)
{
    // Pad 11 toggles Hold mode.
    if(pad == kHoldPad)
    {
        holdEnabled = !holdEnabled;
        if(holdEnabled)
        {
            hw.SetLed(true);
        }
        else
        {
            // Disabling Hold releases every selected planet.
            for(uint16_t i = 0; i < kNumPads; i++)
            {
                padStates[i] = false;
                constellation.ReleasePlanet(i);
            }
            hw.SetLed(false);
        }
        return;
    }

    if(pad == kShiftPad)
    {
        shiftHeld = true;
        return;
    }

    // Pads 0-9 select planets.
    if(pad < kNumPads)
    {
        // In Hold the pads add up like when you play them together.
        // Touching a planet that is already held lets it go.
        if(holdEnabled && padStates[pad])
        {
            padStates[pad] = false;
            constellation.ReleasePlanet(pad);
            hw.SetLed(AnyPadActive());
            return;
        }

        // In Hold with every main voice taken, the new pad replaces the
        // pad that was touched first.
        if(holdEnabled)
        {
            int held = 0;
            int oldest = -1;
            for(int i = 0; i < kNumPads; i++)
            {
                if(padStates[i])
                {
                    ++held;
                    if(oldest < 0 || padOrder[i] < padOrder[oldest])
                        oldest = i;
                }
            }

            if(held >= kNumMainVoices && oldest >= 0)
            {
                padStates[oldest] = false;
                constellation.ReleasePlanet(oldest);
            }
        }

        padOrder[pad] = ++padOrderCounter;
        padStates[pad] = true;
        hw.SetLed(true);
        constellation.AssignPlanet(pad);
    }
}

// ============================================================
// PAD RELEASE CALLBACK
// ============================================================

void OnPadRelease(uint16_t pad)
{
    if(pad == kShiftPad)
    {
        shiftHeld = false;
        return;
    }

    if(pad < kNumPads)
    {
        if(holdEnabled)
            return;

        padStates[pad] = false;
        constellation.ReleasePlanet(pad);
        hw.SetLed(AnyPadActive());
    }
}

// ============================================================
// ACTIVE PAD CHECK
// ============================================================

bool AnyPadActive()
{
    for(int i = 0; i < kNumPads; i++)
    {
        if(padStates[i])
            return true;
    }
    return false;
}

// ============================================================
// AUDIO CALLBACK
// ============================================================

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    cpuMeter.OnBlockStart();

    const bool anyPadActive = AnyPadActive();

    // --------------------------------------------------------
    // PRIMARY CONTROLS
    // --------------------------------------------------------
    const float originValue = knobs.s31().Process();
    const float arcValue    = knobs.s33().Process();

    // How fast origin / Kosmos / arc are being turned: the crystals use it
    // to stop their echoes from repeating a gliding note as a staircase.
    static float lastKosmos = 0.0f;
    const float kosmosNow   = knobs.s32().Process();
    const float kosmosMoved = fabsf(kosmosNow - lastKosmos);
    lastKosmos = kosmosNow;

    // --------------------------------------------------------
    // TRANSIT CONTROL
    // --------------------------------------------------------
    const float transitKnob = knobs.s36().Process();
    const int transitMode   = switches.B();
    if(transitMode == Switch3::POS_CENTER)
    {
        transitPosition = transitKnob;
    }
    else
    {
        float speedPerSample = kTransitSlowPerSample;
        if(transitMode == Switch3::POS_DOWN)
            speedPerSample = kTransitFastPerSample;
        transitPosition += speedPerSample * static_cast<float>(size);
        if(transitPosition > 1.0f)
            transitPosition -= 1.0f;
    }
    transit.SetPosition(transitPosition);

    // --------------------------------------------------------
    // CONSTELLATION CONTROL
    // --------------------------------------------------------
    const float constellationValue = knobs.s30().Process();
    constellation.SetConstellation(constellationValue);

    // --------------------------------------------------------
    // ASPECT DETECTION
    // --------------------------------------------------------
    int aspectA            = -1;
    int aspectB            = -1;
    float aspectDistance   = 0.0f;
    const int currentAspect
        = transit.FindAspect(aspectA, aspectB, aspectDistance);

    float aspectProximity = 0.0f;
    if(currentAspect != ASPECT_NONE)
    {
        aspectProximity
            = transit.GetAspectProximity(aspectDistance, currentAspect);
    }
    AspectData aspect = BuildAspectData(currentAspect, aspectProximity);

    // --------------------------------------------------------
    // MELODY GENERATION
    // Build a note phrase when the detected aspect changes.
    // --------------------------------------------------------
    if(currentAspect != ASPECT_NONE && aspectA >= 0 && aspectB >= 0)
    {
        if(!melodyActive || currentAspect != melodyLastAspect
           || aspectA != melodyLastA || aspectB != melodyLastB)
        {
            // Generator planet is the chord root, folded into 220-880 Hz.
            float rootFrequency = planets[aspectA].frequency;
            while(rootFrequency < 220.0f)
                rootFrequency *= 2.0f;
            while(rootFrequency > 880.0f)
                rootFrequency *= 0.5f;

            const ChordRatios* chord = ChordForAspect(currentAspect);
            if(chord != nullptr)
            {
                melodyNotes[0] = rootFrequency;
                melodyNotes[1] = rootFrequency * chord->second;
                melodyNotes[2] = rootFrequency * chord->third;
                melodyLength   = 3;

                SetNUUTEventChord(&eventVoice,
                                  melodyNotes[0],
                                  melodyNotes[1],
                                  melodyNotes[2]);

                chordFlow.NewChord(melodyNotes[0],
                                   melodyNotes[1],
                                   melodyNotes[2]);
            }
            else
            {
                melodyLength = 0;
            }

            if(melodyLength > 0)
            {
                // Start with the first chord note active.
                melodyStep             = 1;
                melodyCurrentFrequency = melodyNotes[0];
                melodySamplesUntilNext = melodyNoteIntervalSamples;

                // Select the event timbre according to the detected aspect.
                const AspectProfile* profile = GetAspectProfile(currentAspect);
                if(profile != nullptr)
                {
                    SetNUUTEventTimbre(&eventVoice, profile->timbre);

                    // A single-note event (the gong) uses only the root.
                    if(profile->singleNote)
                        eventBellFrequency = melodyNotes[0];
                }

                // Trigger the bell only when a planet is active.
                if(anyPadActive)
                    eventBellTriggerPending = true;

                melodyActive = true;
            }
            else
            {
                melodyActive = false;
            }

            melodyLastAspect = currentAspect;
            melodyLastA      = aspectA;
            melodyLastB      = aspectB;
        }
    }
    else
    {
        melodyActive     = false;
        melodyLastAspect = ASPECT_NONE;
        melodyLastA      = -1;
        melodyLastB      = -1;
    }

    // --------------------------------------------------------
    // SOUND CONTROLS
    // --------------------------------------------------------
    // Origin moves in whole semitones (with a little hysteresis so a
    // knob resting between two steps does not flicker): turning it gives
    // short steps instead of a long slide.
    static float originStep = 0.0f;
    const float originRaw   = originValue * 24.0f - 12.0f;
    if(fabsf(originRaw - originStep) > 0.65f)
        originStep = roundf(originRaw);
    const float originSemitones = originStep;

    // Every origin step (and a slow turn of Kosmos) tells the crystals to
    // let go of their old echoes, so Ametista does not repeat the old note.
    static float lastOriginStep = 0.0f;
    static float movementKick   = 0.0f;
    if(originStep != lastOriginStep)
    {
        movementKick   = 1.0f;
        lastOriginStep = originStep;
    }
    else
    {
        movementKick *= 0.995f;
    }
    crystals.SetMovement(
        fmaxf(movementKick, (kosmosMoved - 0.0001f) * 4000.0f));
    const float originRatio     = powf(2.0f, originSemitones / 12.0f);
    SetNUUTEventOrigin(&eventVoice, originRatio);

    // The Alchemy fader is read with a little electrical noise, and the
    // noise is worst at the ends. Every tiny jump changes the resonator
    // weights and the ethereal stage in one step (a click), and thousands of
    // them are a crackle. So the value is smoothed here (about 80 ms).
    // Second layer: while pad 10 is held, the same fader sets the waveform
    // morph (fader up = sine, down = saw) and Alchemy stays where it was. Each
    // value is picked up again only when the fader crosses it, so nothing
    // jumps when the pad is pressed or released.
    static float alchemyFader   = 0.0f;   // stored Alchemy fader value
    static float morphFader     = 1.0f;   // stored morph value
    static bool  alchemyLocked  = false;
    static bool  morphLocked    = true;
    static bool  lastShift      = false;
    static float alchemySmoothed = 0.0f;
    static float morphSmoothedMain = 1.0f;

    const float alchemyRaw = knobs.s37().Process();
    const bool  shift      = shiftHeld;

    if(shift != lastShift)
    {
        // The fader is now owned by the other function, which must be
        // picked up when the fader reaches its stored value.
        if(shift)
            morphLocked = true;
        else
            alchemyLocked = true;
        lastShift = shift;
    }

    if(!shift)
    {
        if(alchemyLocked && fabsf(alchemyRaw - alchemyFader) < 0.04f)
            alchemyLocked = false;
        if(!alchemyLocked)
            alchemyFader = alchemyRaw;
    }
    else
    {
        if(morphLocked && fabsf(alchemyRaw - morphFader) < 0.04f)
            morphLocked = false;
        if(!morphLocked)
            morphFader = alchemyRaw;
    }

    alchemySmoothed   += 0.012f * (alchemyFader - alchemySmoothed);
    morphSmoothedMain += 0.012f * (morphFader - morphSmoothedMain);
    // The bottom 20% of the fader range is cut: it starts from 0.2.
    const float Alchemy = 0.2f + 0.8f * alchemySmoothed;
    // Fader all up = sine, all down = saw.
    const float waveMorph = 1.0f - morphSmoothedMain;
    ethereal.SetAlchemy(Alchemy);

    const float crystalRefraction = knobs.s34().Process();
    const float crystalRadiance   = crystalRefraction;
    const float crystalWet        = knobs.s35().Process();
    const float crystalDry        = 1.0f - crystalWet;
    const auto crystalMode        = switches.A();

    const float kosmos = kosmosNow;

    // --------------------------------------------------------
    // MELODY TIMER (advanced once per block)
    // Adds one chord note every kMelodyNoteSeconds; notes already
    // activated stay on.
    // --------------------------------------------------------
    int stringNoteIndex = -1;
    if(melodyActive && anyPadActive && melodyLength > 0
       && melodyStep < melodyLength)
    {
        if(melodySamplesUntilNext > size)
        {
            melodySamplesUntilNext -= size;
        }
        else
        {
            ++melodyStep;
            if(melodyStep <= melodyLength)
                stringNoteIndex = melodyStep - 1;
            melodySamplesUntilNext = melodyNoteIntervalSamples;
        }
    }

    const bool eventTrigger = eventBellTriggerPending && anyPadActive;
    eventBellTriggerPending = false;

    const bool eventIsString = eventVoice.timbre == NUUTEventTimbre::String;
    if(eventTrigger && eventIsString)
        stringNoteIndex = 0;

    const bool blockEventTrigger
        = eventIsString ? (stringNoteIndex >= 0) : eventTrigger;

    // --------------------------------------------------------
    // PER-BLOCK VOICE STATE
    // Constellation lookups and voice activation are resolved
    // here instead of once per sample per voice.
    // --------------------------------------------------------
    const float sampleRate = hw.AudioSampleRate();
    // In Hold the chord stays still: no more notes moving up and down.
    chordFlow.SetFrozen(holdEnabled);
    chordFlow.Update(static_cast<float>(size) / sampleRate);

    bool mainGate[kNumMainVoices];
    for(int v = 0; v < kNumMainVoices; v++)
    {
        const int planet = constellation.GetPlanet(v);
        mainGate[v]      = (planet != -1) && padStates[planet];
    }

    const bool chordPad = melodyActive && anyPadActive
                          && AspectUsesChordPad(currentAspect);

    for(int idx = 0; idx < kNumExtraVoices; idx++)
    {
        const int v             = idx + kNumMainVoices;
        const int planet        = constellation.GetPlanet(v);
        const bool isChordVoice = idx >= kChordFirstExtra
                                  && idx <= kChordLastExtra;

        if(isChordVoice && chordPad)
        {
            // Chord that slowly evolves, only for the aspects chosen above.
            const int noteIndex = idx - kChordFirstExtra;
            if(noteIndex < melodyLength)
            {
                constellationVoices[idx].active = true;
                constellationVoices[idx].targetFrequency
                    = chordFlow.Frequency(noteIndex) * originRatio;

                // Slow entry and exit, only while it acts as a chord.
                constellationVoices[idx].attackIncrement
                    = 1.0f / (1.5f * sampleRate);
                constellationVoices[idx].releaseIncrement
                    = 1.0f / (2.0f * sampleRate);
            }
            else
            {
                constellationVoices[idx].active = false;
            }
        }
        else if(planet != -1 && constellation.IsExtraVoice(v)
                && anyPadActive)
        {
            // Normal constellation voice.
            constellationVoices[idx].attackIncrement
                = 1.0f / (0.03f * sampleRate);
            constellationVoices[idx].releaseIncrement
                = 1.0f / (0.30f * sampleRate);
            constellationVoices[idx].active = true;
            constellationVoices[idx].targetFrequency
                = planets[planet].frequency * originRatio;
        }
        else
        {
            constellationVoices[idx].active = false;
        }
    }

    // --------------------------------------------------------
    // EVENT MODULATION (aspect-driven amplitude LFO)
    // One sinf at each end of the block, linear ramp in between.
    // --------------------------------------------------------
    static float eventModPhase = 0.0f;

    float eventModRate  = 0.0f;
    float eventModDepth = 0.0f;

    if(anyPadActive)
    {
        const AspectProfile* modProfile = GetAspectProfile(currentAspect);
        if(modProfile != nullptr)
        {
            eventModRate  = modProfile->modRateHz;
            eventModDepth = modProfile->modDepth;
        }
    }

    // Brightness of the event breathes too. When there is no aspect the
    // last setting stays, so a ringing tail does not jump.
    const AspectProfile* brightProfile = GetAspectProfile(currentAspect);
    if(brightProfile != nullptr)
    {
        SetNUUTEventBrightness(&eventVoice,
                               brightProfile->brightRateHz,
                               brightProfile->brightDepthOct,
                               brightProfile->brightCeilingHz);
    }

    float lfoAmp = 1.0f;
    float lfoInc = 0.0f;
    if(eventModDepth > 0.0f)
    {
        const float startPhase = eventModPhase;

        eventModPhase += eventModRate * static_cast<float>(size) / sampleRate;
        if(eventModPhase >= 1.0f)
            eventModPhase -= 1.0f;

        const float startAmp
            = 1.0f + sinf(startPhase * kTwoPi) * eventModDepth;
        const float endAmp
            = 1.0f + sinf(eventModPhase * kTwoPi) * eventModDepth;

        lfoAmp = startAmp;
        lfoInc = (endAmp - startAmp) / static_cast<float>(size);
    }

    // --------------------------------------------------------
    // AUDIO PROCESSING (per sample)
    // --------------------------------------------------------
    const float refFrequency = voices[0].frequency;
    (void)refFrequency; 

    for(size_t i = 0; i < size; i++)
    {
        // Event voice
        const bool firstSample = (i == 0);
        const float eventSig   = ProcessNUUTEventVoice(
            &eventVoice,
            firstSample && blockEventTrigger,
            eventBellFrequency,
            firstSample ? stringNoteIndex : -1);

        // Main planetary voices.
        float planetSig = 0.0f;
        for(int v = 0; v < kNumMainVoices; v++)
        {
            planetSig += ProcessVoice(&voices[v],
                                      mainGate[v],
                                      0.0f,
                                      &aspect,
                                      originRatio,
                                      arcValue,
                                      Alchemy,
                                      kosmos,
                                      v,
                                      voices[0].frequency,
                                      nullptr,
                                      waveMorph);
        }

        // Extra constellation voices.
        float constellationSig = 0.0f;
        for(int idx = 0; idx < kNumExtraVoices; idx++)
        {
            constellationSig += ProcessPlanetConstellationVoice(
                &constellationVoices[idx],
                constellationVoices[idx].targetFrequency);
        }

        // ----------------------------------------------------
        // VOICE MIX
        // ----------------------------------------------------
        float sig = planetSig + constellationSig * kConstellationGain
                    + eventSig * kEventGain;

        sig += in[0][i] * kMicGain;

        sig *= lfoAmp;
        lfoAmp += lfoInc;

        // ----------------------------------------------------
        // ALCHEMY STAGE (global): sweeping low-pass + slow comb
        // ----------------------------------------------------
        const float colored = ethereal.ProcessColor(sig);

        // ----------------------------------------------------
        // CRYSTAL EFFECTS (mono in, stereo out)
        // ----------------------------------------------------
        CrystalStereo crystalSig = crystals.Process(
            colored, crystalMode, crystalRefraction, crystalRadiance, anyPadActive,
            Alchemy);

        float outputL = colored * crystalDry + crystalSig.left * crystalWet;
        float outputR = colored * crystalDry + crystalSig.right * crystalWet;

        // ----------------------------------------------------
        // GLOBAL ECHO (level follows Alchemy, always a little present)
        // ----------------------------------------------------
        float echoL = 0.0f;
        float echoR = 0.0f;
        ethereal.ProcessEcho(colored, echoL, echoR);
        outputL += echoL;
        outputR += echoR;

        // ----------------------------------------------------
        // GLOBAL STEREO REVERB (DaisySP ReverbSc)
        // Last in the chain, so it washes over the crystals and the echo.
        // ----------------------------------------------------
        float reverbOutL = 0.0f;
        float reverbOutR = 0.0f;
        reverb.Process(outputL, outputR, &reverbOutL, &reverbOutR);

        outputL = outputL * kReverbDryGain + reverbOutL * kReverbWetGain;
        outputR = outputR * kReverbDryGain + reverbOutR * kReverbWetGain;

        // ----------------------------------------------------
        // OUTPUT STAGE
        // ----------------------------------------------------
        out[0][i] = FastTanh(outputL * kOutputGain);
        out[1][i] = FastTanh(outputR * kOutputGain);
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
    hw.SetAudioBlockSize(kAudioBlockSize);

#if NUUT_CPU_LOG
    hw.StartLog(false);
    hw.PrintLine("NUUT CPU monitor started");
#endif

    knobs.Init(hw);
    hw.SetLed(true);
    System::Delay(1000);
    hw.SetLed(false);
    pads.Init();
    switches.Init();
    transit.Init();

    const float sampleRate = hw.AudioSampleRate();
    cpuMeter.Init(sampleRate, hw.AudioBlockSize());

    melodyNoteIntervalSamples
        = static_cast<size_t>(kMelodyNoteSeconds * sampleRate);

    InitNUUTEventVoice(&eventVoice, sampleRate);
    ethereal.Init(sampleRate);
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

    reverb.SetFeedback(kReverbFeedback);
    reverb.SetLpFreq(kReverbLpHz);

    crystals.Init(sampleRate);

    for(int i = 0; i < kNumVoiceSlots; i++)
    {
        InitVoice(&voices[i], sampleRate);
    }

    for(int i = 0; i < kNumExtraVoices; i++)
    {
        InitPlanetConstellationVoice(constellationVoices[i], sampleRate);
    }

    constellation.Init(voices);
    pads.SetOnTouch(OnPadTouch);
    pads.SetOnRelease(OnPadRelease);
    hw.StartAudio(AudioCallback);

#if NUUT_CPU_LOG
    int logCounter = 0;
#endif
    while(true)
    {
        pads.Process();
        System::Delay(4);
#if NUUT_CPU_LOG
        if(++logCounter >= 250) // ~1 second
        {
            logCounter = 0;
            hw.PrintLine("CPU avg %d%% max %d%%",
                         static_cast<int>(cpuMeter.GetAvgCpuLoad() * 100.0f),
                         static_cast<int>(cpuMeter.GetMaxCpuLoad() * 100.0f));
        }
#endif
    }
}