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
#include "nuut/ether.h"

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

constexpr float kTwoPi = 6.2831853f;

// Transit 
constexpr float kTransitSlowPerSample = 0.00005f / 48.0f;
constexpr float kTransitFastPerSample = 0.00015f / 48.0f;

constexpr float kConstellationGain = 1.10f;
constexpr float kEventGain         = 0.005f;
constexpr float kMicGain           = 0.5f;

// Reverb
constexpr float kReverbFeedback    = 0.93f;
constexpr float kReverbLpHz        = 7000.0f;
constexpr float kReverbDryGain     = 1.0f;
constexpr float kReverbWetGain     = 0.34f;
constexpr float kOutputGain        = 0.7f;

constexpr float kMelodyNoteSeconds = 0.5f;


constexpr float kSemi4  = 1.259921f;
constexpr float kSemi5  = 1.334840f;
constexpr float kSemi7  = 1.498307f;
constexpr float kSemi9  = 1.681793f;
constexpr float kSemi10 = 1.781797f;

struct ChordRatios
{
    float second;
    float third;
};

constexpr ChordRatios kChordConjunction = {kSemi5, kSemi9};  // 0, +5, +9
constexpr ChordRatios kChordSextile     = {kSemi5, kSemi10}; // 0, +5, +10
constexpr ChordRatios kChordTrine       = {kSemi4, kSemi7};  // 0, +4, +7
constexpr ChordRatios kChordOpposition  = {kSemi4, kSemi7};  // 0, +4, +7
constexpr ChordRatios kChordSquare      = {kSemi4, kSemi9};  // 0, +4, +9

const ChordRatios* ChordForAspect(int aspect)
{
    switch(aspect)
    {
        case ASPECT_CONJUNCTION: return &kChordConjunction;
        case ASPECT_SEXTILE: return &kChordSextile;
        case ASPECT_TRINE: return &kChordTrine;
        case ASPECT_OPPOSITION: return &kChordOpposition;
        case ASPECT_SQUARE: return &kChordSquare;
        default: return nullptr;
    }
}

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

// ============================================================
// GLOBAL CONTROL STATE
// ============================================================
// Written from the main loop (touch callbacks) and read from the
// audio interrupt, hence volatile.

volatile bool padStates[kNumPads] = {false};

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

    // Pads 0-9 select planets.
    if(pad < kNumPads)
    {
        if(holdEnabled)
        {
        
            for(uint16_t i = 0; i < kNumPads; i++)
            {
                padStates[i] = false;
                constellation.ReleasePlanet(i);
            }
        }
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
                switch(currentAspect)
                {
                    case ASPECT_SQUARE:
                        SetNUUTEventTimbre(&eventVoice,
                                           NUUTEventTimbre::TribalDrum);
                        break;

                    case ASPECT_OPPOSITION:
                        SetNUUTEventTimbre(&eventVoice, NUUTEventTimbre::Gong);
                        // One Gong strike: use only the first note.
                        eventBellFrequency = melodyNotes[0];
                        break;

                    case ASPECT_CONJUNCTION:
                        SetNUUTEventTimbre(&eventVoice,
                                           NUUTEventTimbre::CrystalBell);
                        break;

                    case ASPECT_SEXTILE:
                        SetNUUTEventTimbre(&eventVoice,
                                           NUUTEventTimbre::String);
                        break;

                    case ASPECT_TRINE:
                        SetNUUTEventTimbre(&eventVoice,
                                           NUUTEventTimbre::Flute);
                        break;

                    default:
                        break;
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
    const float originSemitones = originValue * 24.0f - 12.0f;
    const float originRatio     = powf(2.0f, originSemitones / 12.0f);

    const float Alchemy = knobs.s37().Process();
    ethereal.SetAlchemy(Alchemy);

    const float crystalRefraction = knobs.s34().Process();
    const float crystalRadiance   = crystalRefraction;
    const float crystalWet        = knobs.s35().Process();
    const float crystalDry        = 1.0f - crystalWet;
    const auto crystalMode        = switches.A();

    const float kosmos = knobs.s32().Process();

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
            if(melodyStep < melodyLength)
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
    bool mainGate[kNumMainVoices];
    for(int v = 0; v < kNumMainVoices; v++)
    {
        const int planet = constellation.GetPlanet(v);
        mainGate[v]      = (planet != -1) && padStates[planet];
    }

    const bool chordBlocked = (currentAspect == ASPECT_OPPOSITION
                               || currentAspect == ASPECT_SEXTILE);

    for(int idx = 0; idx < kNumExtraVoices; idx++)
    {
        const int v             = idx + kNumMainVoices;
        const int planet        = constellation.GetPlanet(v);
        const bool isChordVoice = idx >= kChordFirstExtra
                                  && idx <= kChordLastExtra;

        if(isChordVoice && chordBlocked)
        {
            constellationVoices[idx].active = false;
        }
        else if(isChordVoice && melodyActive && anyPadActive)
        {
            const int noteIndex = idx - kChordFirstExtra;
            if(noteIndex < melodyLength)
            {
                constellationVoices[idx].active = (melodyStep > noteIndex);
                constellationVoices[idx].targetFrequency
                    = melodyNotes[noteIndex] * originRatio;
            }
            else
            {
                constellationVoices[idx].active = false;
            }
        }
        else if(planet != -1 && constellation.IsExtraVoice(v)
                && anyPadActive)
        {
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
        switch(currentAspect)
        {
            case ASPECT_CONJUNCTION:
                eventModRate  = 0.18f;
                eventModDepth = 0.07f;
                break;
            case ASPECT_SEXTILE:
                eventModRate  = 0.45f;
                eventModDepth = 0.04f;
                break;
            case ASPECT_SQUARE:
                eventModRate  = 2.0f;
                eventModDepth = 0.10f;
                break;
            case ASPECT_TRINE:
                eventModRate  = 0.12f;
                eventModDepth = 0.05f;
                break;
            case ASPECT_OPPOSITION:
                eventModRate  = 0.65f;
                eventModDepth = 0.08f;
                break;
            default:
                break;
        }
    }

    float lfoAmp = 1.0f;
    float lfoInc = 0.0f;
    if(eventModDepth > 0.0f)
    {
        const float sampleRate = hw.AudioSampleRate();
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
                                      nullptr);
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
            colored, crystalMode, crystalRefraction, crystalRadiance, anyPadActive);

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