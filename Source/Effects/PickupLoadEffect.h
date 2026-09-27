#pragma once

#include "EffectProcessor.h"
#include "GuitarSource.h"

#include <cmath>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    The guitar's own pickup (series R + L) and a standard instrument cable (a shunt C) loaded by the pedal's own
    input impedance, in front of the pedal: a real second-order network, not a divider. See GuitarSource.h for why
    this is universal (every real guitar rig has it, whichever pedal it feeds) and docs/circuits/PickupLoading.md
    for the derivation.

    Circuit: DI (the pickup's own open-circuit voltage) -> Rp + Lp (series) -> node (Cc to ground, Rin to ground,
    Rin being the pedal's own input impedance -- its first resistor to ground/bias, already in its own netlist; this
    wrapper only adds what is in front of it). That is a plain 2-pole, no-zero lowpass:
        H(s) = Rin / ( Lp Cc Rin s^2 + (Lp + Rp Cc Rin) s + (Rp + Rin) )
    -- solved here for its corner frequency and Q and run as a TPT (Andrew Simper) state-variable filter, which is
    exact at any sample rate and stable at any Q. A HIGH input impedance (500k-1M, most op-amp pedals) under-damps
    it into a resonant peak around 3-6 kHz before it rolls off; a LOW one (a fuzz's tens of kilohms) over-damps it
    into a smooth rolloff starting lower -- the two sound different for the same reason a passive guitar sounds
    different into a wah than into a buffer, and it is why every pedal here converging on the same brightness was a
    sign this was missing everywhere, not a coincidence.
*/
class PickupLoadEffect : public EffectProcessor
{
public:
    PickupLoadEffect (std::unique_ptr<EffectProcessor> innerProcessor, double inputImpedanceOhms)
        : inner (std::move (innerProcessor)), rin (inputImpedanceOhms) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        const double rp = guitarSource::resistance, lp = guitarSource::inductance, cc = guitarSource::cableCapacitance;
        const double a2 = lp * cc * rin, a1 = lp + rp * cc * rin, a0 = rp + rin;
        const double wc = std::sqrt (a0 / a2);
        // A bare series-R/L, shunt-C model overstates the resonance (Q above 6 for a 1M input): real pickups have
        // extra damping this project does not model separately (eddy-current/core losses in the coil), which a
        // detailed equivalent circuit puts in parallel with Lp. Capped here instead of adding that element: a sharper
        // peak would itself sound synthetic/resonant, the opposite of "more natural".
        const double q = std::min (std::sqrt (a0 * a2) / a1, 2.0);
        dcGain = rin / a0; // the divider's own DC ratio: H(0) = Rin / (Rp + Rin)

        // TPT (Andrew Simper) state-variable filter coefficients: g = tan(pi * fc / fs), clamped below Nyquist for a
        // pathologically low sample rate (never happens in practice; wc is a few kHz).
        const double gCoeff = std::tan (juce::jmin (wc / (2.0 * sampleRate), 0.49) * juce::MathConstants<double>::pi);
        k = 1.0 / q;
        a1c = 1.0 / (1.0 + gCoeff * (gCoeff + k));
        a2c = gCoeff * a1c;
        a3c = gCoeff * a2c;

        state.assign ((size_t) std::max (1, numChannels), { 0.0, 0.0 });
        inner->prepare (sampleRate, maxBlockSize, numChannels);
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const int channels = std::min (buffer.getNumChannels(), (int) state.size());
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto& s = state[(size_t) ch];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const double v0 = (double) d[i] * dcGain;
                const double v3 = v0 - s.ic2;
                const double v1 = a1c * s.ic1 + a2c * v3;
                const double v2 = s.ic2 + a2c * s.ic1 + a3c * v3;
                s.ic1 = 2.0 * v1 - s.ic1;
                s.ic2 = 2.0 * v2 - s.ic2;
                d[i] = (float) v2; // the lowpass output
            }
        }
        inner->process (buffer);
    }

    void reset() override
    {
        for (auto& s : state)
            s = {};
        inner->reset();
    }

    juce::AudioProcessorParameterGroup* getParameters() override { return inner->getParameters(); }
    std::unique_ptr<juce::XmlElement> getState() const override { return inner->getState(); }
    void setState (const juce::XmlElement& s) override { inner->setState (s); }
    const char* getName() const override { return inner->getName(); }
    juce::String getStatusText() const override { return inner->getStatusText(); }
    juce::Colour getAccentColour() const override { return inner->getAccentColour(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { inner->drawIcon (g, b); }

    EffectProcessor& getInner() noexcept { return *inner; }

private:
    struct State { double ic1 = 0.0, ic2 = 0.0; };

    std::unique_ptr<EffectProcessor> inner;
    double rin;
    double dcGain = 1.0, k = 1.0, a1c = 0.0, a2c = 0.0, a3c = 0.0;
    std::vector<State> state;
};

} // namespace openguitarmultifx
