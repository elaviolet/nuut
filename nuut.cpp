// FLASH: the audio code is compiled for speed (-O2) even though the project
// uses -Os. If the build runs out of flash, set NUUT_FAST to 0 first.
#define NUUT_FAST 1
#if NUUT_FAST
#define NUUT_O2 __attribute__((optimize("O2")))
#else
#define NUUT_O2
#endif
#include "daisy_seed.h"
#include <new>
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

// MIDI over USB (the web pages follow the Touch). 0 = off.
#define NUUT_MIDI 1
#if NUUT_MIDI
#include "nuut/nuut_midi.h"
NuutMidiState g_nuutMidiState;
static NuutMidi nuutMidi;
#endif

using namespace daisy;
using namespace daisysp;

// Set to 0 to disable the once-per-second CPU load print over USB serial.
#define NUUT_CPU_LOG 0   // must stay 0 while NUUT_MIDI is 1 (both use the USB port)

// Per-section cost, cycles per sample (budget about 10000). Only active with
// NUUT_CPU_LOG 1. Sections: events, main voices, extra voices, comb/peak,
// crystals, echo, reverb.
#if NUUT_CPU_LOG
namespace prof
{
constexpr int kN = 7;
volatile uint32_t acc[kN];
volatile uint32_t sum[kN];
volatile uint32_t mx[kN];
volatile uint32_t blocks;
}
#define PROF_BEGIN() uint32_t profT = DWT->CYCCNT
#define PROF_END(k) do { const uint32_t n_ = DWT->CYCCNT; prof::acc[k] += n_ - profT; profT = n_; } while(0)
#else
#define PROF_BEGIN() do {} while(0)
#define PROF_END(k) do {} while(0)
#endif

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

// Which planet each touch pad plays (pad 0..9 -> planet index).
// Planet indices: 0 Mercury, 1 Venus, 2 Sun, 3 Mars, 4 Jupiter,
// 5 Saturn, 6 Uranus, 7 Neptune, 8 Pluto, 9 Moon.
// Several planets have almost the same Cousto note (Mercury/Pluto,
// Mars/Saturn, Uranus/Moon/Neptune), so the duplicates sit on the pads
// that are hardest to play (0..3) and the easy pads play six different notes.
constexpr int kPadToPlanet[kNumPads] = {
    0,  // pad 0: Mercury (C#3, same as Pluto)
    3,  // pad 1: Mars    (D3, same as Saturn)
    6,  // pad 2: Uranus  (G#3, same as Moon)
    7,  // pad 3: Neptune (G#3, same as Uranus)
    4,  // pad 4: Jupiter (F#3)
    5,  // pad 5: Saturn  (D3)
    9,  // pad 6: Moon    (G#3)
    1,  // pad 7: Venus   (A3)
    8,  // pad 8: Pluto   (C#3)
    2,  // pad 9: Sun     (B2)
};

constexpr int kHoldPad = 11;

// Pad 10 is a modifier: while it is held, the Alchemy fader
// controls the waveform of the main voices instead of Alchemy.
constexpr int kShiftPad = 10;
volatile bool shiftHeld = false;

constexpr float kTwoPi = 6.2831853f;

// Transit switch (top / middle / bottom): Linger / Off / On.
//   Off    the sky stands still; the Transit fader moves it (relative).
//   On     the fader sets the speed: one full cycle in 120 s ... 5 s.
//   Linger like On, but the sky slows down when two planets near an aspect.
// Shift + Transit fader = amount of melody. Shift + Hold = freeze the sky.
constexpr float kCycleSlowSeconds = 120.0f;
constexpr float kCycleFastSeconds = 5.0f;

// Planet personalities (traditional character -> musical behaviour).
// The planet that was pressed first (the chord root) sets the rhythm and
// the touch; the other planet sets the voicing (inversion) of the chord.
// Order: Mercury Venus Sun Mars Jupiter Saturn Uranus Neptune Pluto Moon.
struct PlanetPersona
{
    float period;     // spacing of the melody (1 = normal)
    float irregular;  // how uneven the spacing is (0..1)
    float touch;      // strength of the event (0..1)
    uint8_t voicing;  // inversion given when it is the second planet (0..2)
};
constexpr PlanetPersona kPersona[10] = {
    {0.30f, 0.5f, 0.65f, 1},  // Mercury: fast, light, unpredictable
    {0.75f, 0.1f, 0.75f, 0},  // Venus:   singing, even
    {1.00f, 0.1f, 0.90f, 0},  // Sun:     full, confident
    {0.50f, 0.0f, 1.00f, 0},  // Mars:    marked, regular, strong
    {1.10f, 0.2f, 0.80f, 1},  // Jupiter: wide, rising
    {2.40f, 0.1f, 0.70f, 0},  // Saturn:  few notes, slow
    {0.90f, 1.0f, 0.70f, 2},  // Uranus:  sudden, uneven
    {1.80f, 0.4f, 0.50f, 2},  // Neptune: soft, drifting
    {3.00f, 0.3f, 0.80f, 0},  // Pluto:   rare, deep
    {0.80f, 0.7f, 0.60f, 1},  // Moon:    changeable
};
// Minimum time (s) between two melody gestures: the instrument must be
// able to finish its phrase (same order as the AspectType: bell, string,
// drum, flute, gong).
constexpr float kGestureSeconds[5] = {3.5f, 4.0f, 1.6f, 5.0f, 8.0f};

constexpr float kConstellationGain = 1.10f;
constexpr float kEventGain         = 0.008f;
constexpr float kMicGain           = 0.25f;

// Reverb
constexpr float kReverbFeedback    = 0.93f;
constexpr float kReverbLpHz        = 7000.0f;
constexpr float kReverbDryGain     = 1.0f;
constexpr float kReverbWetGain     = 0.34f;
constexpr float kOutputGain        = 0.6f;   // was 0.7: it was clipping

// CPU PROFILING SWITCHES. Everything is on for normal use. To find out what
// costs what, set ONE of these to false, build, and read the CPU log: the drop
// in "avg" is the cost of that part (with a pad held and the fader at normal).
constexpr bool kEnableReverb   = true;   // global stereo reverb
constexpr bool kEnableCrystals = true;   // the crystal effects
constexpr bool kEnableColor    = true;   // Alchemy stage: comb + bright peak
constexpr bool kEnableEcho     = true;   // Alchemy stage: stereo echo
constexpr bool kEnableEvents   = true;   // aspect event voice (bell, string, ...)


// Chords, instruments, modulation and the chord pad of each aspect are
// in nuut/events.h, in the table "ASPECT PROFILES" (one row per aspect).

NUUT_O2 inline float FastTanh(float x)
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
// Counts the samples the event voice has been silent. When it is silent for
// long enough its resonators are cleared and not processed: tiny leftovers
// (denormal numbers) in the dead resonators were costing about 24% of the CPU.
uint32_t eventQuietSamples = 0;

// Global Alchemy stage: sweeping filter, slow comb and stereo echo.
// Its delay lines (about 100 KB) live in the external SDRAM: with the
// bootloader (BOOT_SRAM) the variables must fit in the 128 KB DTCM. The object
// is built in main(), after the SDRAM has been started.
DSY_SDRAM_BSS uint8_t etherealMem[sizeof(EtherealStage)] __attribute__((aligned(8)));
EtherealStage& ethereal = *reinterpret_cast<EtherealStage*>(etherealMem);

// Global stereo delay used for the main reverb.
DSY_SDRAM_BSS daisysp::ReverbSc reverb;

Pads pads;
Knobs knobs;
Switches switches;
Constellation constellation;
Transit transit;
// The crystal delay lines (about 224 KB) live in the external SDRAM: the
// internal SRAM was full once the USB MIDI buffers were added. The object is
// built in main(), after the SDRAM has been started.
DSY_SDRAM_BSS uint8_t crystalsMem[sizeof(Crystals)] __attribute__((aligned(8)));
Crystals& crystals = *reinterpret_cast<Crystals*>(crystalsMem);

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

// Freeze (Shift + Hold): the sky stops where it is.
volatile bool skyFrozen = false;

constexpr int MELODY_LENGTH = 3;
float melodyNotes[MELODY_LENGTH] = {};

int melodyLastAspect = ASPECT_NONE;
int melodyLastA      = -1;
int melodyLastB      = -1;

bool melodyActive                = false;
bool lastEventWasGong            = false;
int  gongRepeats                 = 0;   // repeats of the gong in this aspect
float melodyTimer                = 0.0f;   // seconds until the next gesture
float eventTouch                 = 0.8f;   // strength of the events (planet)
float eventTouchTarget           = 0.8f;
uint32_t melodyRng               = 2463534242u;

bool AnyPadActive();

// ============================================================
// PAD TOUCH CALLBACK
// ============================================================

void OnPadTouch(uint16_t pad)
{
    // Pad 11 toggles Hold mode.
    if(pad == kHoldPad && shiftHeld)
    {
        skyFrozen = !skyFrozen;
#if NUUT_MIDI
        nuutMidi.Freeze(skyFrozen);
#endif
        return;
    }

    if(pad == kHoldPad)
    {
        holdEnabled = !holdEnabled;
#if NUUT_MIDI
        nuutMidi.Hold(holdEnabled);
#endif
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
#if NUUT_MIDI
        nuutMidi.Shift(true);
#endif
        return;
    }

    // Pads 0-9 select planets.
    if(pad < kNumPads)
    {
#if NUUT_MIDI
        nuutMidi.Pad(pad, true);
#endif
        const uint16_t planet = kPadToPlanet[pad];
        // In Hold the pads add up like when you play them together.
        // Touching a planet that is already held lets it go.
        if(holdEnabled && padStates[planet])
        {
            padStates[planet] = false;
            constellation.ReleasePlanet(planet);
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

        padOrder[planet] = ++padOrderCounter;
        padStates[planet] = true;
        hw.SetLed(true);
        constellation.AssignPlanet(planet);
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
#if NUUT_MIDI
        nuutMidi.Shift(false);
#endif
        return;
    }

    if(pad < kNumPads)
    {
#if NUUT_MIDI
        nuutMidi.Pad(pad, false);
#endif
        const uint16_t planet = kPadToPlanet[pad];
        if(holdEnabled)
            return;

        padStates[planet] = false;
        constellation.ReleasePlanet(planet);
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

NUUT_O2
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

    const float sampleRate = hw.AudioSampleRate();
    const float blockSeconds = static_cast<float>(size) / sampleRate;

    // --------------------------------------------------------
    // SHIFT LAYER
    // While Shift is held, the Alchemy fader sets the waveform and the
    // Transit fader sets the amount of melody. Each value is picked up again
    // only when the fader crosses it, so nothing jumps when Shift changes.
    // --------------------------------------------------------
    static bool  alchemyLocked = false;
    static bool  morphLocked   = true;
    static bool  transitLocked = false;
    static bool  melodyLocked  = true;
    static bool  lastShift     = false;
    static float transitFader  = 0.0f;   // stored Transit fader value
    static float melodyFader   = 0.4f;   // stored melody amount
    const bool shift = shiftHeld;
    if(shift != lastShift)
    {
        if(shift)
        {
            morphLocked  = true;
            melodyLocked = true;
        }
        else
        {
            alchemyLocked = true;
            transitLocked = true;
        }
        lastShift = shift;
    }

    // --------------------------------------------------------
    // TRANSIT CONTROL
    // --------------------------------------------------------
    const float transitKnob = knobs.s36().Process();
    const int transitSwitch = switches.B();   // UP Linger, CENTER Off, DOWN On
    float transitDelta = 0.0f;                // fader movement (Off mode)
    static bool transitStarted = false;
    if(!transitStarted)
    {
        transitStarted = true;
        transitFader   = transitKnob;         // no jump at power-up
    }
    if(!shift)
    {
        if(transitLocked)
        {
            if(fabsf(transitKnob - transitFader) < 0.04f)
            {
                transitLocked = false;
                transitFader  = transitKnob;
            }
        }
        else
        {
            transitDelta = transitKnob - transitFader;
            transitFader = transitKnob;
        }
    }
    else
    {
        if(melodyLocked && fabsf(transitKnob - melodyFader) < 0.04f)
            melodyLocked = false;
        if(!melodyLocked)
            melodyFader = transitKnob;
    }

    // Only the planets of the three main voices can form an aspect.
    unsigned aspectMask = 0;
    for(int v = 0; v < kNumMainVoices; v++)
    {
        const int p = constellation.GetPlanet(v);
        if(p >= 0 && p < 10)
            aspectMask |= 1u << p;
    }

    if(!skyFrozen)
    {
        if(transitSwitch == Switch3::POS_CENTER)
        {
            // Off: the fader moves the sky from where it is.
            transitPosition += transitDelta;
        }
        else
        {
            // On / Linger: the fader is the speed of the sky.
            const float cycle = kCycleSlowSeconds
                * powf(kCycleFastSeconds / kCycleSlowSeconds, transitFader);
            float speed = 1.0f;
            if(transitSwitch == Switch3::POS_UP)
                speed = transit.LingerFactor(aspectMask);
            transitPosition += blockSeconds / cycle * speed;
        }
        while(transitPosition >= 1.0f)
            transitPosition -= 1.0f;
        while(transitPosition < 0.0f)
            transitPosition += 1.0f;
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
        = transit.FindAspect(aspectA, aspectB, aspectDistance, aspectMask);

    // The planet pressed first is the root of the chord.
    if(currentAspect != ASPECT_NONE && aspectA >= 0 && aspectB >= 0
       && padOrder[aspectB] < padOrder[aspectA])
    {
        const int t = aspectA;
        aspectA     = aspectB;
        aspectB     = t;
    }

    float aspectProximity = 0.0f;
    if(currentAspect != ASPECT_NONE)
    {
        aspectProximity
            = transit.GetAspectProximity(aspectDistance, currentAspect);
    }
    AspectData aspect = BuildAspectData(currentAspect, aspectProximity);

    // --------------------------------------------------------
    // MELODY GENERATION
    // A new aspect (or a new pair of planets) starts an event: chord,
    // instrument, voicing. While it lasts, the melody layer plays the
    // instrument again and again, at the pace of the root planet.
    //
    // Two protections against a messy overlap of events:
    //  - an aspect that drops out for less than half a second (the sky
    //    jitters at the edge of the orb) is not a new event;
    //  - a new event waits until the last one has had time to sound
    //    (0.6 s; 0.25 s for the drum, so it can follow the planets rhythmically;
    //    3 s after a gong; a second gong waits 10 s after the last one).
    // --------------------------------------------------------
    static float noAspectTime  = 0.0f;
    static float sinceEvent    = 99.0f;
    static bool  startDeferred = false;
    sinceEvent += blockSeconds;
    static float sinceGong = 99.0f;
    sinceGong += blockSeconds;

    const AspectProfile* profile = GetAspectProfile(currentAspect);
    if(profile != nullptr && aspectA >= 0 && aspectB >= 0)
    {
        noAspectTime = 0.0f;
        if(!melodyActive || currentAspect != melodyLastAspect
           || aspectA != melodyLastA || aspectB != melodyLastB)
        {
            melodyActive     = true;
            melodyLastAspect = currentAspect;
            melodyLastA      = aspectA;
            melodyLastB      = aspectB;
            startDeferred    = true;
        }
    }
    else
    {
        noAspectTime += blockSeconds;
        if(noAspectTime > 0.5f)
        {
            melodyActive     = false;
            melodyLastAspect = ASPECT_NONE;
            melodyLastA      = -1;
            melodyLastB      = -1;
            startDeferred    = false;
        }
    }

    if(startDeferred && melodyActive && anyPadActive)
    {
        const AspectProfile* sp = GetAspectProfile(melodyLastAspect);
        // A gong waits for the last gong to ring out; any other event only
        // waits a moment (3 s after a gong, so its tail is not cut at once).
        // The gong answers at once, like the drum (it only keeps a short
        // distance from the last gong, so a planet at the edge of the orb
        // cannot strike it again and again).
        const bool quick = sp->timbre == NUUTEventTimbre::TribalDrum || sp->singleNote;
        const float need = quick ? 0.25f : (lastEventWasGong ? 1.5f : 0.6f);
        if(sinceEvent >= need && (!sp->singleNote || sinceGong >= 1.5f))
        {
            // The root planet is the root of the chord, folded into 220-880 Hz.
            float rootFrequency = planets[melodyLastA].frequency;
            while(rootFrequency < 220.0f)
                rootFrequency *= 2.0f;
            while(rootFrequency > 880.0f)
                rootFrequency *= 0.5f;

            melodyNotes[0] = rootFrequency;
            melodyNotes[1] = rootFrequency * sp->chord.second;
            melodyNotes[2] = rootFrequency * sp->chord.third;

            SetNUUTEventChord(&eventVoice,
                              melodyNotes[0],
                              melodyNotes[1],
                              melodyNotes[2]);
            // The second planet chooses the voicing of the chord.
            eventVoice.inversion = kPersona[melodyLastB].voicing;

            SetNUUTEventTimbre(&eventVoice, sp->timbre);
            // A single-note event (the gong) uses only the root.
            if(sp->singleNote)
                eventBellFrequency = melodyNotes[0];

            eventTouchTarget = kPersona[melodyLastA].touch;
            eventVoice.repeatGain = 1.0f;
            melodyTimer      = kGestureSeconds[melodyLastAspect];

            eventBellTriggerPending = true;
            sinceEvent       = 0.0f;
            if(sp->singleNote)
                sinceGong = 0.0f;
            lastEventWasGong = sp->singleNote;
            gongRepeats      = 0;
            startDeferred    = false;
        }
    }

    // Melody layer: the same event is played again when the timer runs out.
    // The root planet sets the pace, the melody amount (Shift + Transit
    // fader) sets how often.
    // The gong (single-note event) rings once per aspect: it does not repeat.
    // The gong repeats at most twice, far apart (a gong that keeps sounding
    // is a mess); the other instruments go on for as long as the aspect lasts.
    const bool lastIsGong = melodyActive && GetAspectProfile(melodyLastAspect)->singleNote;
    if(melodyActive && !startDeferred && anyPadActive && melodyFader > 0.03f
       && (!lastIsGong || gongRepeats < 1))
    {
        melodyTimer -= blockSeconds;
        if(melodyTimer <= 0.0f)
        {
            melodyRng = melodyRng * 1664525u + 1013904223u;
            const float r = static_cast<float>(melodyRng >> 8) / 16777216.0f;
            const PlanetPersona& pa = kPersona[melodyLastA];
            // The drum repeats faster (a rhythm), the others take their time.
            const float pace = melodyLastAspect == 2 ? 2.4f : 4.0f;
            const float gap = pa.period * pace / (0.35f + 1.4f * melodyFader)
                              * (1.0f + pa.irregular * (r - 0.5f) * 1.4f);
            melodyTimer = kGestureSeconds[melodyLastAspect] + gap;
            eventVoice.repeatGain = 0.5f;   // repeats are softer (gong)
            if(lastIsGong)
            {
                ++gongRepeats;
                // Steady tolls: same distance, same weight, a little softer.
                melodyTimer = 8.0f;
                eventVoice.repeatGain = 0.6f;
            }

            // Variation: every repeat is a little different.
            melodyRng = melodyRng * 1664525u + 1013904223u;
            const float r2 = static_cast<float>(melodyRng >> 8) / 16777216.0f;
            melodyRng = melodyRng * 1664525u + 1013904223u;
            const float r3 = static_cast<float>(melodyRng >> 8) / 16777216.0f;
            melodyRng = melodyRng * 1664525u + 1013904223u;
            const float r4 = static_cast<float>(melodyRng >> 8) / 16777216.0f;

            // Voicing: half of the time the one of the second planet,
            // otherwise any of the three.
            eventVoice.inversion = r2 < 0.5f
                ? kPersona[melodyLastB].voicing
                : static_cast<int>(r3 * 3.0f) % 3;
            // Touch: stronger or softer, within a quarter.
            eventTouchTarget = lastIsGong ? kPersona[melodyLastA].touch
                                          : kPersona[melodyLastA].touch * (0.75f + 0.25f * r3);
            // Now and then the instrument rests: one gesture is skipped
            // (never the gong: its tolls are steady).
            if(!lastIsGong && r4 < 0.2f)
                melodyTimer = kGestureSeconds[melodyLastAspect] * 0.6f + gap;
            else
            {
                eventBellTriggerPending = true;
                sinceEvent = 0.0f;
            }
        }
    }
    eventTouch += 0.05f * (eventTouchTarget - eventTouch);
    // Melody at zero: the events play their old single gesture, once.
    eventVoice.phrasesOn = melodyFader > 0.03f;

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
    static float alchemySmoothed = 0.0f;
    static float morphSmoothedMain = 1.0f;

    const float alchemyRaw = knobs.s37().Process();

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
    const float Alchemy = 0.2f + 0.8f * alchemySmoothed;
    // Fader all up = sine, all down = saw.
    const float waveMorph = 1.0f - morphSmoothedMain;
    ethereal.SetAlchemy(Alchemy);
    // FM + brightness amount: starts gently a little below the top of the
    // fader (at 65%) and rises smoothly (smoothstep) to 1 at the very bottom.
    const float bLin = fminf(1.0f, fmaxf(0.0f, (0.65f - alchemySmoothed) / 0.65f));
    const float Bright = bLin * bLin * (3.0f - 2.0f * bLin);
    ethereal.SetBrightness(Bright);

    const float crystalRefraction = knobs.s34().Process();
    const float crystalRadiance   = crystalRefraction;
    const float crystalWet        = knobs.s35().Process();
    const float crystalDry        = 1.0f - crystalWet;
    const auto crystalMode        = switches.A();

#if NUUT_MIDI
    // Values for the web pages (sent from the main loop, not from here).
    g_nuutMidiState.ctrl[0] = constellationValue;   // Celestia
    g_nuutMidiState.ctrl[1] = originValue;
    g_nuutMidiState.ctrl[2] = kosmosNow;
    g_nuutMidiState.ctrl[3] = arcValue;
    g_nuutMidiState.ctrl[4] = crystalRefraction;
    g_nuutMidiState.ctrl[5] = crystalWet;
    g_nuutMidiState.ctrl[6] = transitFader;
    g_nuutMidiState.ctrl[7] = alchemyFader;
    g_nuutMidiState.melody  = melodyFader;
    g_nuutMidiState.morph   = morphFader;
    g_nuutMidiState.transitPos = transitPosition;
    // Web: 0 obsidian, 1 amethyst, 2 aquamarine
    g_nuutMidiState.crystal
        = crystalMode == Switch3::POS_DOWN ? 0
          : (crystalMode == Switch3::POS_CENTER ? 1 : 2);
    // Web (CC41): 0 = Linger (up), 64 = Off (middle), 127 = On (down).
    g_nuutMidiState.transitMode
        = transitSwitch == Switch3::POS_UP ? 0
          : (transitSwitch == Switch3::POS_CENTER ? 1 : 2);
    g_nuutMidiState.aspect
        = currentAspect == ASPECT_NONE ? 0 : currentAspect + 1;
    g_nuutMidiState.aspectA = aspectA < 0 ? 0 : aspectA;
    g_nuutMidiState.aspectB = aspectB < 0 ? 0 : aspectB;
    g_nuutMidiState.proximity = aspectProximity;
#endif

    // KOSMOS: zone 0 = planets as they are, zones 1..9 = nine chords built on
    // the root (voice 1). The zone has a little
    // hysteresis so a noisy knob cannot flicker between two chords.
    static int kosmosZone = 0;
    {
        constexpr float kZoneWidth  = 1.0f / kKosmosZones;
        constexpr float kHysteresis = 0.012f;
        const float lo = kosmosZone * kZoneWidth - kHysteresis;
        const float hi = (kosmosZone + 1) * kZoneWidth + kHysteresis;
        if(kosmosNow < lo || kosmosNow > hi)
        {
            int z = static_cast<int>(kosmosNow * kKosmosZones);
            if(z < 0)
                z = 0;
            if(z >= kKosmosZones)
                z = kKosmosZones - 1;
            kosmosZone = z;
        }
    }
    // Frequency ratios that put voices 2 and 3 on their chord degrees.
    const float kosmosRootHz = voices[0].frequency;
    float kosmosRatio[kNumMainVoices] = {1.0f, 1.0f, 1.0f};
    kosmosRatio[1] = KosmosDegreeRatio(kosmosRootHz, voices[1].frequency,
                                       kosmosZone, 0);
    kosmosRatio[2] = KosmosDegreeRatio(kosmosRootHz, voices[2].frequency,
                                       kosmosZone, 1);

    const bool eventTrigger = eventBellTriggerPending && anyPadActive;
    eventBellTriggerPending = false;

    // The string plays its whole gesture on the first note index.
    const bool eventIsString = eventVoice.timbre == NUUTEventTimbre::String;
    const int stringNoteIndex = (eventTrigger && eventIsString) ? 0 : -1;
    const bool blockEventTrigger = eventTrigger;

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

    for(int idx = 0; idx < kNumExtraVoices; idx++)
    {
        const int v      = idx + kNumMainVoices;
        const int planet = constellation.GetPlanet(v);

        if(planet != -1 && constellation.IsExtraVoice(v)
                && anyPadActive)
        {
            // Normal constellation voice.
            constellationVoices[idx].attackIncrement
                = 1.0f / (0.03f * sampleRate);
            constellationVoices[idx].releaseIncrement
                = 1.0f / (0.30f * sampleRate);
            constellationVoices[idx].active = true;
            // Celestia voices follow the same chord as the main voices.
            constellationVoices[idx].targetFrequency
                = planets[planet].frequency * originRatio
                  * KosmosSnapRatio(kosmosRootHz, planets[planet].frequency,
                                    kosmosZone, -1, nullptr);
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

    // Strength of the event: set by the root planet (about 1 at 0.8).
    const float eventTouchScale = eventTouch * 1.25f;

    // --------------------------------------------------------
    // AUDIO PROCESSING (per sample)
    // --------------------------------------------------------
    const float refFrequency = voices[0].frequency;
    (void)refFrequency; 

    for(size_t i = 0; i < size; i++)
    {
        PROF_BEGIN();
        // Event voice
        const bool firstSample = (i == 0);
        float eventSig = 0.0f;
        if(kEnableEvents)
        {
            const bool evTrigger = firstSample && blockEventTrigger;
            if(evTrigger || eventQuietSamples < 4800u)
            {
                eventSig = ProcessNUUTEventVoice(
                    &eventVoice,
                    evTrigger,
                    eventBellFrequency,
                    firstSample ? stringNoteIndex : -1);

                if(!evTrigger && fabsf(eventSig) < 1e-6f)
                {
                    if(++eventQuietSamples == 4800u)
                    {
                        // Silent: clear the resonators, keep the timbre.
                        const NUUTEventTimbre keep = eventVoice.timbre;
                        InitNUUTEventVoice(&eventVoice, sampleRate);
                        eventVoice.timbre = keep;
                    }
                }
                else
                {
                    eventQuietSamples = 0;
                }
            }
        }
        PROF_END(0);

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
                                      kosmosRatio[v],
                                      v,
                                      voices[0].frequency,
                                      nullptr,
                                      waveMorph,
                                      Bright);
        }

        PROF_END(1);

        // Extra constellation voices.
        float constellationSig = 0.0f;
        for(int idx = 0; idx < kNumExtraVoices; idx++)
        {
            constellationSig += ProcessPlanetConstellationVoice(
                &constellationVoices[idx],
                constellationVoices[idx].targetFrequency);
        }

        PROF_END(2);

        // ----------------------------------------------------
        // VOICE MIX
        // ----------------------------------------------------
        float sig = planetSig + constellationSig * kConstellationGain
                    + eventSig * kEventGain * eventTouchScale;

        sig += in[0][i] * kMicGain;

        sig *= lfoAmp;
        lfoAmp += lfoInc;

        // ----------------------------------------------------
        // ALCHEMY STAGE (global): sweeping low-pass + slow comb
        // ----------------------------------------------------
        const float colored = kEnableColor ? ethereal.ProcessColor(sig) : sig;

        PROF_END(3);

        // ----------------------------------------------------
        // CRYSTAL EFFECTS (mono in, stereo out)
        // ----------------------------------------------------
        CrystalStereo crystalSig = {0.0f, 0.0f};
        if(kEnableCrystals)
        {
            crystalSig = crystals.Process(
                colored, crystalMode, crystalRefraction, crystalRadiance,
                anyPadActive, Alchemy);
        }

        float outputL = colored * crystalDry + crystalSig.left * crystalWet;
        float outputR = colored * crystalDry + crystalSig.right * crystalWet;
        PROF_END(4);

        // ----------------------------------------------------
        // GLOBAL ECHO (level follows Alchemy, always a little present)
        // ----------------------------------------------------
        float echoL = 0.0f;
        float echoR = 0.0f;
        if(kEnableEcho)
            ethereal.ProcessEcho(colored, echoL, echoR);
        outputL += echoL;
        outputR += echoR;
        PROF_END(5);

        // ----------------------------------------------------
        // GLOBAL STEREO REVERB (DaisySP ReverbSc)
        // Last in the chain, so it washes over the crystals and the echo.
        // ----------------------------------------------------
        float reverbOutL = 0.0f;
        float reverbOutR = 0.0f;
        if(kEnableReverb)
        {
            reverb.Process(outputL, outputR, &reverbOutL, &reverbOutR);

            outputL = outputL * kReverbDryGain + reverbOutL * kReverbWetGain;
            outputR = outputR * kReverbDryGain + reverbOutR * kReverbWetGain;
        }

        PROF_END(6);

        // ----------------------------------------------------
        // OUTPUT STAGE
        // ----------------------------------------------------
        out[0][i] = FastTanh(outputL * kOutputGain);
        out[1][i] = FastTanh(outputR * kOutputGain);
    }

#if NUUT_CPU_LOG
    for(int k = 0; k < prof::kN; k++)
    {
        const uint32_t perSample = prof::acc[k] / static_cast<uint32_t>(size);
        prof::acc[k] = 0;
        prof::sum[k] += perSample;
        if(perSample > prof::mx[k])
            prof::mx[k] = perSample;
    }
    prof::blocks++;
#endif
    cpuMeter.OnBlockEnd();
}

// ============================================================
// MAIN
// ============================================================
// Set-up code runs once: keep it small (-Os) to save flash.
int main()
{
    hw.Configure();
    hw.Init();

    // Flush denormal numbers to zero. Decaying feedback loops (reverb, echo,
    // resonators) end in tiny denormal values, which the FPU can handle slowly
    // and cause short CPU peaks. FPSCR covers the main loop; FPDSCR (0xE000EF3C)
    // is the default for interrupt handlers, where the audio callback runs.
    {
        uint32_t fpscr;
        __asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24);
        __asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
        *reinterpret_cast<volatile uint32_t*>(0xE000EF3C) |= (1u << 24);
    }
    hw.SetAudioBlockSize(kAudioBlockSize);

#if NUUT_CPU_LOG
    hw.StartLog(false);
    hw.PrintLine("NUUT CPU monitor started");
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
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

    InitNUUTEventVoice(&eventVoice, sampleRate);
    new(etherealMem) EtherealStage();
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

    new(crystalsMem) Crystals();
    crystals.Init(sampleRate);

    for(int i = 0; i < kNumVoiceSlots; i++)
    {
        InitVoice(&voices[i], sampleRate);
        // The modal resonators refresh their coefficients every 64 samples.
        // Offset the voices so the three refreshes never land in the same
        // sample: it flattens the CPU peaks that cause crackle.
        voices[i].resonator.updateCounter = static_cast<std::size_t>((i * 21) & 63);
    }

    for(int i = 0; i < kNumExtraVoices; i++)
    {
        InitPlanetConstellationVoice(constellationVoices[i], sampleRate);
    }

    constellation.Init(voices);
    pads.SetOnTouch(OnPadTouch);
    pads.SetOnRelease(OnPadRelease);
#if NUUT_MIDI
    nuutMidi.Init();
#endif
    hw.StartAudio(AudioCallback);

#if NUUT_CPU_LOG
    int logCounter = 0;
#endif
    while(true)
    {
        pads.Process();
#if NUUT_MIDI
        static int mt = 0;
        if(++mt >= 5)
        {
            mt = 0;
            nuutMidi.Update();
        }
#endif
        System::Delay(4);
#if NUUT_CPU_LOG
        if(++logCounter >= 250) // ~1 second
        {
            logCounter = 0;
            hw.PrintLine("CPU avg %d%% max %d%%",
                         static_cast<int>(cpuMeter.GetAvgCpuLoad() * 100.0f),
                         static_cast<int>(cpuMeter.GetMaxCpuLoad() * 100.0f));
            const uint32_t nb = prof::blocks ? prof::blocks : 1;
            hw.PrintLine("  cyc/sample avg|max  ev %d|%d  voc %d|%d  ext %d|%d  comb %d|%d  cry %d|%d  echo %d|%d  rev %d|%d",
                         (int)(prof::sum[0] / nb), (int)prof::mx[0],
                         (int)(prof::sum[1] / nb), (int)prof::mx[1],
                         (int)(prof::sum[2] / nb), (int)prof::mx[2],
                         (int)(prof::sum[3] / nb), (int)prof::mx[3],
                         (int)(prof::sum[4] / nb), (int)prof::mx[4],
                         (int)(prof::sum[5] / nb), (int)prof::mx[5],
                         (int)(prof::sum[6] / nb), (int)prof::mx[6]);
            for(int k = 0; k < prof::kN; k++)
            {
                prof::sum[k] = 0;
                prof::mx[k]  = 0;
            }
            prof::blocks = 0;
        }
#endif
    }
}