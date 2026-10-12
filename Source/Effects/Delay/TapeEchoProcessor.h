#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/EPStyleBoosterProcessor.h"
#include "Effects/SpringReverbProcessor.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"

#include <array>

namespace openguitarmultifx
{

enum class TapeEchoModel
{
    spaceEcho, // Roland RE-201: three playback heads, Repeat Rate = motor speed, 12-position Mode selector, spring reverb
    echoplex,  // Maestro Echoplex EP-3: one playback head, the record head slides (Delay), its own preamp in front
    echorec    // Binson Echorec 2: a magnetic drum, four heads at fixed spacing, 12-position selector, Swell
};

/**
    Magnetic-medium echoes (docs/circuits/TapeEchoes.md): a tape loop or drum moving past a record head and one or more
    playback heads. Each head's delay is its distance from the record head divided by the medium's speed, and the speed
    wanders (wow ~0.7 Hz, flutter ~9 Hz), so every repeat is slightly detuned the way a real transport detunes it.
    Recording saturates (the repeats compress and thicken as they build); playback loses treble through the head gap
    (worse the slower the medium moves); repeats go back through the record amp, so at high Intensity / Sustain / Swell
    the echo runs away into the classic self-oscillation, bounded by the saturation.
*/
class TapeEchoProcessor : public EffectProcessor
{
public:
    explicit TapeEchoProcessor (TapeEchoModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb5793f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Shelf // RBJ shelving biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double sr, double hz, double gainDb, bool high);
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };

    struct Channel
    {
        EnsembleDelay tape;
        WetLowpass headLoss;
        double recordHighpass = 0.0, lastPlayback = 0.0;
        Shelf bass, treble;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0);
    static double saturate (double x) noexcept;

    TapeEchoModel model;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *mode = nullptr, *speed = nullptr, *intensity = nullptr, *echoLevel = nullptr,
                              *reverbLevel = nullptr, *bass = nullptr, *treble = nullptr, *recordLevel = nullptr;

    std::unique_ptr<SpringReverbProcessor> spring;     // RE-201
    std::unique_ptr<EPStyleBoosterProcessor> preamp;   // EP-3's record preamp, the same circuit as the EP Booster
    juce::AudioBuffer<float> scratch;
    juce::SmoothedValue<float> smoothedSpeed;

    double wowPhase = 0.0, flutterPhase = 0.0, sampleRate = 0.0;
    float lastBass = -1.0f, lastTreble = -1.0f;
};

} // namespace openguitarmultifx
