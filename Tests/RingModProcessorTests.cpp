#include "Effects/RingModProcessor.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

class RingModProcessorTests : public juce::UnitTest
{
public:
    RingModProcessorTests() : juce::UnitTest ("RingModProcessor", "Effects") {}

    static constexpr double sr = 192000.0; // as if oversampled 4x from 48 kHz (the registry wraps the processor)

    /** Amplitude of one frequency in a rendered block (single-bin DFT over a whole number of cycles of `base`). */
    static double bin (const std::vector<float>& x, double freq)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < x.size(); ++n)
        {
            re += x[n] * std::cos (twoPi * freq * (double) n / sr);
            im += x[n] * std::sin (twoPi * freq * (double) n / sr);
        }
        return 2.0 * std::sqrt (re * re + im * im) / (double) x.size();
    }

    static std::vector<float> render (RingModProcessor& p, double inFreq, double amp, double seconds)
    {
        const int n = (int) (seconds * sr);
        std::vector<float> out ((size_t) n);
        juce::AudioBuffer<float> buf (1, 512);
        int done = 0;
        while (done < n)
        {
            const int len = std::min (512, n - done);
            buf.setSize (1, len, false, false, true);
            for (int i = 0; i < len; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * inFreq * (double) (done + i) / sr)));
            p.process (buf);
            for (int i = 0; i < len; ++i)
                out[(size_t) (done + i)] = buf.getSample (0, i);
            done += len;
        }
        return out;
    }

    void runTest() override
    {
        beginTest ("Parker's diode shaper: dead below the bias, a quadratic knee, then affine with slope h");
        {
            expectEquals (RingModProcessor::diode (0.1), 0.0);
            expectEquals (RingModProcessor::diode (-0.15), 0.0);
            expectWithinAbsoluteError (RingModProcessor::diode (0.3), 1.0 * 0.1 * 0.1 / 0.4, 1.0e-12);
            expectWithinAbsoluteError (RingModProcessor::diode (0.4), 0.1, 1.0e-12); // (0.4 - 0.2)^2 / (2 * 0.4 - 2 * 0.2)
            expectWithinAbsoluteError (RingModProcessor::diode (1.4) - RingModProcessor::diode (0.9), 0.5, 1.0e-12); // slope h = 1 above vL
            expectEquals (RingModProcessor::diode (-0.7), RingModProcessor::diode (0.7));
        }

        beginTest ("a sine through a sine carrier gives the SUM and DIFFERENCE frequencies, and suppresses the carrier and the input");
        {
            RingModProcessor p;
            p.prepare (sr, 512, 1);
            auto params = p.getParameters()->getParameters (true);
            *dynamic_cast<juce::AudioParameterFloat*> (params[0]) = 1000.0f; // carrier 1 kHz
            *dynamic_cast<juce::AudioParameterFloat*> (params[3]) = 1.0f;    // fully wet
            render (p, 300.0, 0.3, 0.2);                                       // settle
            const auto y = render (p, 300.0, 0.3, 0.5);
            const double diff = bin (y, 700.0), sum = bin (y, 1300.0), carrierBin = bin (y, 1000.0), inBin = bin (y, 300.0);
            logMessage ("700 Hz " + juce::String (diff, 4) + ", 1300 Hz " + juce::String (sum, 4) + ", carrier " + juce::String (carrierBin, 5)
                        + ", input " + juce::String (inBin, 5));
            expectGreaterThan (diff, 0.05);
            expectGreaterThan (sum, 0.05);
            expectLessThan (carrierBin, 0.03 * diff);
            expectLessThan (inBin, 0.03 * diff);
        }

        beginTest ("Mix 0 is the input untouched; Mix 1 is about as loud as the input");
        {
            RingModProcessor p;
            p.prepare (sr, 512, 1);
            auto params = p.getParameters()->getParameters (true);
            *dynamic_cast<juce::AudioParameterFloat*> (params[3]) = 0.0f;
            render (p, 300.0, 0.3, 0.1);
            const auto dry = render (p, 300.0, 0.3, 0.2);
            expectWithinAbsoluteError (bin (dry, 300.0), 0.3, 0.002);

            *dynamic_cast<juce::AudioParameterFloat*> (params[3]) = 1.0f;
            render (p, 300.0, 0.3, 0.2);
            const auto wet = render (p, 300.0, 0.3, 0.5);
            double eIn = 0.0, eOut = 0.0;
            for (size_t i = 0; i < wet.size(); ++i)
            {
                const double x = 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * (double) i / sr);
                eIn += x * x;
                eOut += (double) wet[i] * wet[i];
            }
            const double db = 10.0 * std::log10 (eOut / eIn);
            logMessage ("wet vs dry energy " + juce::String (db, 2) + " dB");
            expectWithinAbsoluteError (db, 0.0, 3.0);
        }

        beginTest ("every Shape at every Drive: finite, bounded, and a carrier-free output when the input is silent");
        for (float shape : { 0.0f, 1.0f, 2.0f })
            for (float drive : { 0.0f, 0.5f, 1.0f })
            {
                RingModProcessor p;
                p.prepare (sr, 512, 2);
                auto params = p.getParameters()->getParameters (true);
                *dynamic_cast<juce::AudioParameterFloat*> (params[1]) = drive;
                *dynamic_cast<juce::AudioParameterFloat*> (params[2]) = shape;
                *dynamic_cast<juce::AudioParameterFloat*> (params[3]) = 1.0f;
                juce::AudioBuffer<float> buf (2, 512);
                float peak = 0.0f;
                for (int b = 0; b < 100; ++b)
                {
                    buf.clear();
                    p.process (buf);
                    peak = juce::jmax (peak, buf.getMagnitude (0, 512));
                }
                expectLessThan (peak, 1.0e-6f); // the bridge cancels the carrier exactly with no input
                const auto y = render (p, 220.0, 0.8, 0.2);
                for (float v : y)
                    expect (std::isfinite (v) && std::abs (v) < 10.0f);
            }
    }
};

static RingModProcessorTests ringModProcessorTests;

} // namespace openguitarmultifx
