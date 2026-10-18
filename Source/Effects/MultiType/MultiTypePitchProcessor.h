#pragma once

#include "MultiTypeCommon.h"

#include <array>
#include <atomic>

namespace openguitarmultifx
{

/** One multi-voice pitch engine; the types are presets of it.

      mono sum -> one shared delay line -> up to 4 voices, each a two-grain time-domain shifter:
          read pointer A at  base + voiceDelay + phi * grain
          read pointer B at  base + voiceDelay + frac (phi + 0.5) * grain
          phi advances by (1 - ratio) / grain per sample, so the delay slides and the pitch changes by `ratio`
          the two reads are cross-faded with complementary smooth windows (their sum is always exactly 1)
      ratio = 2^((shift + detune/100 + LFO) / 12), glided with a one-pole
      -> per-voice level and pan -> low / high cut -> dry/wet mix.

    Three ways the semitone shift of a voice is chosen (Mode):
      FIXED     the voice's Shift parameter
      WHAMMY    a pedal position (0..100 %) swept over a range (UP/DOWN 1 or 2 octaves, or UP|DN)
      DIATONIC  a small time-domain pitch detector finds the played note, the note is placed in the Key/Scale, and each
                voice moves it by its Harmony steps (scale degrees), so the interval is a major or minor third as needed.

    There is no FFT anywhere: the cost per voice is two interpolated reads and a handful of multiplies. */
class MultiTypePitchProcessor : public EffectProcessor
{
public:
    MultiTypePitchProcessor();
    explicit MultiTypePitchProcessor (int fixedTypeIndex);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool hasTrails() const override { return true; }
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    juce::String getStatusText() const override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Pitch" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc9702f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    enum class Mode { fixed, whammy, diatonic };
    static constexpr int kVoices = 4;
    struct TypeSpec
    {
        const char* name;
        Mode mode;
        int voices;
        float shift[kVoices], detune[kVoices], delayMs[kVoices], levelDb[kVoices], pan[kVoices];
        int steps[kVoices];
        float mix, grainMs, lfoRate, lfoDepth, glideMs;
        int whammyRange; // index into whammyRangeNames()
    };
    static const std::vector<TypeSpec>& types();
    static juce::StringArray whammyRangeNames() { return { "UP 1 OCT", "DOWN 1 OCT", "UP 2 OCT", "DOWN 2 OCT", "UP|DN 1 OCT", "UP|DN 2 OCT" }; }

private:
    /** Small time-domain pitch detector: decimate by 4, normalised difference function over the lag range, first dip
        under a threshold, parabolic refinement. Fixed cost, no allocation. */
    struct Detector
    {
        static constexpr int dec = 4, ringN = 1024, win = 256, hop = 128;
        void prepare (double fs);
        void reset();
        /** True when a new estimate was produced on this sample. */
        bool push (float x);
        float hz = 0.0f;

    private:
        double fsd = 12000.0;
        float lp = 0.0f, coeff = 0.1f;
        int phase = 0, w = 0, count = 0;
        std::array<float, ringN> ring {};
        std::array<float, ringN> cmnd {};
        void analyse();
    };

    void applyType (int index);
    void updateDiatonicShifts();
    float whammyShift (float position) const;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *mode, *voices, *mix, *level;
    juce::AudioParameterFloat *shift[kVoices], *detune[kVoices], *delay[kVoices], *voiceLevel[kVoices], *pan[kVoices], *step[kVoices];
    juce::AudioParameterFloat *grain, *lfoRate, *lfoDepth, *glide, *lowCut, *highCut;
    juce::AudioParameterFloat *whammyPos, *whammyRange, *key, *scale;
    mt::TypeWatcher watcher;
    std::string fixedName;
    std::vector<std::unique_ptr<juce::AudioParameterFloat>> hiddenParams;

    double fs = 48000.0;
    mt::DelayLine line;
    Detector detector;
    mt::Lfo lfo;
    mt::OnePole lc[2], hc[2];
    double phi[kVoices] {}, semis[kVoices] {}, ratio[kVoices] { 1, 1, 1, 1 };
    float diatonicShift[kVoices] {};
    int ratioCounter = 0;
    std::atomic<float> detectedHz { 0.0f };
    mt::MixLevel mixLevel;
};

} // namespace openguitarmultifx
