#pragma once

#include "PolyPitchTracker.h"
#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Polyphonic guitar synth: a spectral note tracker (up to six simultaneous notes, so chords and double stops work)
    gives each detected note its own oscillator voice; an envelope follower drives the gate, the dynamics and the
    filter sweep.
      guitar -> PolyPitchTracker (96 ms FFT every 6 ms, harmonic grouping, persistent note slots)
             -> one voice per note: oscillator (saw / square / triangle / sine, PolyBLEP) + detuned twin at
                note * octave, glided; the sub-oscillator (one octave down) rides on the lowest note only
             -> sum (normalised by 1/sqrt(notes)) -> SVF low-pass -> soft clip -> mix with the dry guitar.
    A single-note line keeps one voice gliding from note to note, so Glide still behaves like portamento. */
class TrackedSynthProcessor : public SynthEffect
{
public:
    TrackedSynthProcessor();
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

private:
    struct Voice
    {
        gs::Osc osc, osc2, sub;
        double hz = 0.0, targetHz = 0.0, amp = 0.0, weight = 1.0;
        uint32_t slotId = 0;
        int slot = -1;
        int releasedAt = -100000; // sample counter when its note ended
    };

    void assignVoices (double octMul);

    juce::AudioParameterFloat *wave, *octave, *sub, *detune, *glide, *cutoff, *resonance, *envAmount, *release, *dynamics,
        *sensitivity, *dryGuitar;
    double fs = 48000.0;
    gs::PolyPitchTracker tracker;
    gs::EnvelopeFollower env;
    std::array<Voice, gs::PolyPitchTracker::kMaxNotes> voices {};
    gs::Svf filter;
    double norm = 1.0;
    int lowest = -1, clock = 0;
    bool gate = false;
    int controlCounter = 0;
};

} // namespace openguitarmultifx
