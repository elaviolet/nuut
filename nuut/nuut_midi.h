#pragma once
// NUUT -> USB MIDI (class-compliant), so the web pages can follow the Touch.
// Enable with  #define NUUT_MIDI 1  (and keep NUUT_CPU_LOG 0: both use the USB port).
// Map (same as nuut-shared.js, MIDI_MAP):
//  ch1  Note On/Off 36+pad (P00..P09), 46 = Shift, 47 = Hold, 48 = Freeze
//       CC30 Celestia, 31 Origin, 32 Kosmos, 33 Arc, 34 Refraction, 35 Crystal mix,
//       36 Transit, 37 Alchemy, 38 melody, 39 morph, CC40 crystal switch (0/64/127),
//       CC41 transit mode (0 Linger, 64 Off, 127 On), CC42+CC74 sky position (14 bit)
//  ch2  CC20 aspect (0 none, 1..5), CC21 planet A, CC22 planet B, CC23 proximity
#include "daisy_seed.h"
#include "hid/midi.h"
#include <cmath>
#include <cstdint>

struct NuutMidiState
{
    // written by the audio callback (floats 0..1), read by the main loop
    volatile float ctrl[8]      = {0, 0, 0, 0, 0, 0, 0, 0}; // CC30..CC37
    volatile int   crystal      = 1;  // 0 obsidian, 1 amethyst, 2 aquamarine
    volatile int   transitMode  = 1;  // 0 linger, 1 off, 2 on
    volatile int   aspect       = 0;  // 0 none, 1..5
    volatile int   aspectA      = 0;
    volatile int   aspectB      = 0;
    volatile float proximity    = 0.f;
    volatile float melody       = 0.4f; // CC38 (Shift + Transit fader)
    volatile float morph        = 1.f;  // CC39 (Shift + Alchemy fader)
    volatile float transitPos   = 0.f;  // 0..1 position of the sky: CC42 (MSB) + CC74 (LSB)
};
extern NuutMidiState g_nuutMidiState;

class NuutMidi
{
  public:
    void Init()
    {
        daisy::MidiUsbHandler::Config cfg;
        cfg.transport_config.periph = daisy::MidiUsbTransport::Config::INTERNAL;
        midi_.Init(cfg);
    }
    void Pad(int pad, bool on)
    {
        if(pad >= 0 && pad < 16) { if(on) heldMask_ |= (1u << pad); else heldMask_ &= ~(1u << pad); }
        Send(0x90, 36 + pad, on ? 100 : 0);
    }
    void Shift(bool on) { Send(0x90, 46, on ? 100 : 0); }
    void Hold(bool on) { Send(0x90, 47, on ? 100 : 0); }
    void Freeze(bool on) { Send(0x90, 48, on ? 100 : 0); }
    // call every ~20 ms from the main loop: only changed values are sent
    void Update()
    {
        // Safety refresh (~0.7 s): a USB message can be dropped, so the aspect
        // and the released pads are sent again. They are idempotent on the page.
        if(++refresh_ >= 35)
        {
            refresh_ = 0;
            lastAsp_ = -1;
            for(int p = 0; p < 10; p++)
                if(!(heldMask_ & (1u << p))) Send(0x90, 36 + p, 0);
        }
        for(int i = 0; i < 8; i++)
        {
            const uint8_t v = ToCc(g_nuutMidiState.ctrl[i]);
            if(v != lastCtrl_[i]) { lastCtrl_[i] = v; Send(0xB0, 30 + i, v); }
        }
        {
            const uint8_t m = ToCc(g_nuutMidiState.melody);
            if(m != lastMelody_) { lastMelody_ = m; Send(0xB0, 38, m); }
            const uint8_t mo = ToCc(g_nuutMidiState.morph);
            if(mo != lastMorph_) { lastMorph_ = mo; Send(0xB0, 39, mo); }
            float tp = g_nuutMidiState.transitPos;
            tp = tp < 0.f ? 0.f : tp > 0.99993f ? 0.99993f : tp;
            const int t14 = (int)(tp * 16384.f);
            if(t14 != lastPos_ && (t14 - lastPos_ > 3 || lastPos_ - t14 > 3 || lastPos_ < 0))
            {
                lastPos_ = t14;
                Send(0xB0, 74, (uint8_t)(t14 & 0x7F));
                Send(0xB0, 42, (uint8_t)(t14 >> 7));
            }
        }
        const uint8_t cs = g_nuutMidiState.crystal == 0 ? 0 : g_nuutMidiState.crystal == 1 ? 64 : 127;
        if(cs != lastCrystal_) { lastCrystal_ = cs; Send(0xB0, 40, cs); }
        const uint8_t tm = g_nuutMidiState.transitMode == 0 ? 0 : g_nuutMidiState.transitMode == 1 ? 64 : 127;
        if(tm != lastTm_) { lastTm_ = tm; Send(0xB0, 41, tm); }
        const int asp = g_nuutMidiState.aspect, a = g_nuutMidiState.aspectA, b = g_nuutMidiState.aspectB;
        const uint8_t pr = ToCc(g_nuutMidiState.proximity);
        if(asp != lastAsp_ || a != lastA_ || b != lastB_)
        {
            Send(0xB1, 21, (uint8_t)a); Send(0xB1, 22, (uint8_t)b); Send(0xB1, 23, pr); Send(0xB1, 20, (uint8_t)asp);
            lastAsp_ = asp; lastA_ = a; lastB_ = b; lastPr_ = pr;
        }
        else if(asp && (pr > lastPr_ + 3 || pr + 3 < lastPr_)) { lastPr_ = pr; Send(0xB1, 23, pr); }
    }
    // call from the same place the aspect event note is struck (ch2)
    void EventNote(int midiNote, float vel) { Send(0x91, midiNote, (uint8_t)(vel * 127.f)); }

  private:
    static uint8_t ToCc(float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (uint8_t)(v * 127.f + 0.5f); }
    void Send(uint8_t status, uint8_t d1, uint8_t d2)
    {
        uint8_t m[3] = {status, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F)};
        midi_.SendMessage(m, 3);
    }
    daisy::MidiUsbHandler midi_;
    uint8_t lastCtrl_[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    uint8_t lastCrystal_ = 255, lastTm_ = 255, lastPr_ = 0, lastMelody_ = 255, lastMorph_ = 255;
    int lastPos_ = -1;
    unsigned heldMask_ = 0;
    int refresh_ = 0;
    int lastAsp_ = -1, lastA_ = -1, lastB_ = -1;
};