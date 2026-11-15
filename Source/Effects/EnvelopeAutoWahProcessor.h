#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    Auto-wah pedals: an envelope follower on the guitar (or an LFO) tunes a resonant band-pass filter, so the wah
    opens when you play hard and closes as the note dies -- a wah played by your picking dynamics.

    One class, three circuits (docs/circuits/EnvelopeAutoWah.md):

      mxrEnvelopeFilter      the 1979 MXR Envelope Filter (schematic 120-4001): a state-variable filter of three CMOS
                             inverters whose integrator resistors are CD4016 switches chopped at 37 kHz. The switch
                             duty cycle is the tuning (averaged resistance = R / duty); the duty comes from the
                             envelope detector compared against a triangle. Band-pass tap, ~280 Hz .. ~3.2 kHz.
      mxrBassEnvelopeFilter  the same chassis voiced for bass the way the MXR M82 is described (larger integrator
                             caps for a lower sweep, and a Dry/FX blend so the low fundamental is never filtered
                             away). Derived from the MXR circuit, not from the M82's own schematic.
      doctorQ                the Electro-Harmonix Doctor Q: a multiple-feedback band-pass op-amp stage whose tuning
                             resistor is a transistor (BC239C) used as a base-current-controlled resistor, driven by
                             a diode peak detector. Its Norm/Bass switch is the circuit's own bass voicing (input
                             through 470K into the virtual earth: flat lows with a wah peak, instead of a pure
                             band-pass).

    Beyond each real panel, every model has a sweep Mode (Up / Down / LFO) and an LFO Rate; the LFO replaces the
    envelope as the tuning source.
*/
class EnvelopeAutoWahProcessor : public EffectProcessor
{
public:
    enum class Model { mxrEnvelopeFilter, mxrBassEnvelopeFilter, doctorQ };

    explicit EnvelopeAutoWahProcessor (Model m = Model::mxrEnvelopeFilter);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8f6fb0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics (channel 0): the filter's tuning right now as a 0..1 position (0 = lowest, 1 = highest centre
        frequency of that model). */
    double debugTuning() const noexcept { return channels[0].tuning; }

    /** MXR models: band-pass centre frequency (Hz) for a switch duty (the SVF's natural frequency). */
    static double mxrCentreFrequencyHz (double duty, bool bass) noexcept;

    /** Doctor Q: multiple-feedback band-pass natural frequency (Hz) for the transistor's resistance. */
    static double doctorQCentreFrequencyHz (double transistorOhms) noexcept;

    static constexpr double dutyMin = 0.08;  // MXR, no envelope: ~280 Hz
    static constexpr double dutyMax = 0.87;  // MXR, full envelope: ~3.2 kHz

private:
    struct Channel
    {
        // MXR: SVF integrator states and their slopes (trapezoidal rule)
        double bp = 0.0, lp = 0.0, dBp = 0.0, dLp = 0.0;
        double c1 = 0.0, c2 = 0.0, c3 = 0.0, c4 = 0.0; // per-block filter constants
        double duty = dutyMin;
        // Doctor Q: capacitor histories (C N->earth-side, C N->out, input series cap)
        double v1 = 0.0, i1 = 0.0, v2 = 0.0, i2 = 0.0, vIn = 0.0, iIn = 0.0;
        double rv = 2.0e6;       // transistor resistance
        // shared
        double inHpX = 0.0, inHpY = 0.0, outHpX = 0.0, outHpY = 0.0;
        double env = 0.0;        // MXR detector / Doctor Q storage capacitor volts
        double tuning = 0.0;
        int countdown = 0;
    };
    std::array<Channel, 2> channels;

    Model model;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* threshold = nullptr;  // MXR: Threshold; Doctor Q: Sensitivity
    juce::AudioParameterFloat* attack = nullptr;     // MXR only
    juce::AudioParameterFloat* decay = nullptr;      // MXR only
    juce::AudioParameterFloat* peak = nullptr;       // MXR only (Q)
    juce::AudioParameterFloat* rangeTrim = nullptr;  // Doctor Q only
    juce::AudioParameterFloat* voicing = nullptr;    // Doctor Q only: 0 Norm, 1 Bass
    juce::AudioParameterFloat* mode = nullptr;       // 0 Up, 1 Down, 2 LFO
    juce::AudioParameterFloat* rate = nullptr;
    juce::AudioParameterFloat* blend = nullptr;      // bass models: dry/FX

    juce::SmoothedValue<float> smoothedBlend;
    double lfoPhase = 0.0;
    double sampleRate = 0.0;

    bool isMxr() const noexcept { return model != Model::doctorQ; }
    double integratorCap() const noexcept;
    void updateMxrConstants (Channel& ch, double r19) const noexcept;
    void processMxr (juce::AudioBuffer<float>& buffer);
    void processDoctorQ (juce::AudioBuffer<float>& buffer);

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }
};

} // namespace openguitarmultifx
