#pragma once

#include <cmath>
#include <cstdint>

// ============================================================
// CHORD FLOW
// Makes the three chord pad voices move slowly on the same chord,
// instead of sitting still on a block triad.
//
// Each voice plays one note of the chord. Every few seconds, one voice
// (never all together) glides to another note of the chord, sometimes an
// octave lower or higher, so the voicing keeps turning over: inversions,
// open and close positions, always on the same three notes.
//
// Usage:
//   ChordFlow chordFlow;
//   chordFlow.NewChord(n0, n1, n2);       // when the chord changes
//   chordFlow.Update(seconds);            // once per audio block
//   f = chordFlow.Frequency(voice);       // 0..2, before the Origin ratio
// ============================================================

class ChordFlow
{
  public:
    static constexpr int kVoices = 3;

    // Starts a new chord. The voices glide from wherever they are.
    void NewChord(float note0, float note1, float note2)
    {
        notes_[0] = note0;
        notes_[1] = note1;
        notes_[2] = note2;

        for(int v = 0; v < kVoices; ++v)
        {
            tone_[v]    = v;
            octave_[v]  = 0;
            countdown_[v] = 2.0f + 1.5f * static_cast<float>(v)
                            + 2.0f * Rand01();

            if(!started_)
                freq_[v] = Target(v);
        }

        started_ = true;
    }

    // Moves time forward by `seconds` (call once per audio block).
    void Update(float seconds)
    {
        if(!started_)
            return;

        // Short glide: the voice slides to the new note in under a second.
        const float k = 1.0f - expf(-seconds / glideSeconds_);

        for(int v = 0; v < kVoices; ++v)
        {
            if(!frozen_)
                countdown_[v] -= seconds;

            if(countdown_[v] <= 0.0f)
            {
                Move(v);
                countdown_[v] = 4.0f + 4.0f * Rand01();
            }

            freq_[v] += (Target(v) - freq_[v]) * k;
        }
    }

    float Frequency(int voice) const { return freq_[voice]; }

    // While frozen the chord stops turning over (glides still finish).
    void SetFrozen(bool frozen) { frozen_ = frozen; }

    void SetGlideSeconds(float s) { glideSeconds_ = s > 0.05f ? s : 0.05f; }

  private:
    float Target(int v) const
    {
        return notes_[tone_[v]] * OctaveRatio(octave_[v]);
    }

    static float OctaveRatio(int o)
    {
        return o < 0 ? 0.5f : (o > 0 ? 2.0f : 1.0f);
    }

    float Rand01()
    {
        rng_ = rng_ * 1664525u + 1013904223u;

        // Mix the bits, so that consecutive values are not correlated.
        std::uint32_t x = rng_;
        x ^= x >> 16;
        x *= 0x45d9f3bu;
        x ^= x >> 16;

        return static_cast<float>(x >> 8) / 16777216.0f;
    }

    // Voice v goes to another note / octave of the chord.
    void Move(int v)
    {
        for(int attempt = 0; attempt < 8; ++attempt)
        {
            int tone = static_cast<int>(Rand01() * 3.0f) % 3;
            const float r = Rand01();
            int octave = r < 0.10f ? -1 : (r < 0.90f ? 0 : 1);

            // Stay in a pleasant register.
            const float f = notes_[tone] * OctaveRatio(octave);
            if(f > 1100.0f)
                octave -= 1;
            if(notes_[tone] * OctaveRatio(octave) < 150.0f)
                octave += 1;

            if(tone == tone_[v] && octave == octave_[v])
                continue;

            // Do not sit exactly on the same pitch as another voice.
            bool clash = false;
            for(int o = 0; o < kVoices; ++o)
            {
                if(o != v && tone_[o] == tone && octave_[o] == octave)
                    clash = true;
            }

            if(clash)
                continue;

            tone_[v]   = tone;
            octave_[v] = octave;
            return;
        }
    }

    float notes_[3]  = {432.0f, 540.0f, 648.0f};
    int tone_[kVoices]   = {0, 1, 2};
    int octave_[kVoices] = {0, 0, 0};
    float freq_[kVoices] = {432.0f, 540.0f, 648.0f};
    float countdown_[kVoices] = {3.0f, 4.0f, 5.0f};

    float glideSeconds_ = 0.3f;
    bool frozen_        = false;
    bool started_       = false;
    std::uint32_t rng_  = 2468013u;
};