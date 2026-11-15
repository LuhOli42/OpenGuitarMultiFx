#include "Effects/EnvelopeAutoWahProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class EnvelopeAutoWahProcessorTests : public juce::UnitTest
{
public:
    EnvelopeAutoWahProcessorTests() : juce::UnitTest ("EnvelopeAutoWahProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = EnvelopeAutoWahProcessor;
    using Model = P::Model;

    // Parameter order -- MXR: Threshold, Attack, Decay, Peak, (Dry/FX on the bass model), Mode, Rate.
    //                    Doctor Q: Sensitivity, Range, Voicing, Mode, Rate.  Mode: 0 Up, 1 Down, 2 LFO.
    static std::unique_ptr<P> make (Model m, std::initializer_list<float> params)
    {
        auto p = std::make_unique<P> (m);
        setParams (*p, params);
        p->prepare (sr, 512, 1);
        return p;
    }

    /** Gain (dB) at a frequency with a very quiet probe (1 mV), which leaves the envelope at its idle tuning: the
        Up position idles at the bottom of the sweep, Down at the top. */
    static double quietGainDb (Model m, std::initializer_list<float> params, double freq)
    {
        auto p = make (m, params);
        const double amp = 0.001;
        return 20.0 * std::log10 (probeSine (*p, {}, freq, amp, sr, 0.5, 0.2).out / amp);
    }

    static double peakGainFreq (Model m, std::initializer_list<float> params, double* peakDb = nullptr)
    {
        double bestDb = -1.0e9, bestF = 0.0;
        for (double f = 80.0; f < 7000.0; f *= 1.12)
        {
            const double g = quietGainDb (m, params, f);
            if (g > bestDb) { bestDb = g; bestF = f; }
        }
        if (peakDb != nullptr)
            *peakDb = bestDb;
        return bestF;
    }

    /** Tuning position after a sustained note of the given amplitude. */
    static double tuningAfterNote (Model m, std::initializer_list<float> params, double amp)
    {
        auto p = make (m, params);
        probeSine (*p, {}, 220.0, amp, sr, 0.5, 0.1);
        return p->debugTuning();
    }

    void runTest() override
    {
        beginTest ("silence stays silent");
        for (auto m : { Model::mxrEnvelopeFilter, Model::mxrBassEnvelopeFilter, Model::doctorQ })
        {
            P p (m);
            p.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 10; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
                expect (std::abs (buf.getSample (0, i)) < 0.001f);
        }

        beginTest ("MXR: the band-pass sweeps from ~280 Hz (no envelope) to ~3 kHz (full envelope), as the schematic notes");
        {
            const double lo = P::mxrCentreFrequencyHz (P::dutyMin, false);
            const double hi = P::mxrCentreFrequencyHz (P::dutyMax, false);
            logMessage ("analytic centre " + juce::String (lo, 0) + " .. " + juce::String (hi, 0) + " Hz");
            expectWithinAbsoluteError (lo, 285.0, 25.0);
            expectWithinAbsoluteError (hi, 3150.0, 150.0);

            double dbRest = 0.0, dbTop = 0.0;
            const double fRest = peakGainFreq (Model::mxrEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 0.0f, 0.5f }, &dbRest);
            const double fTop = peakGainFreq (Model::mxrEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 1.0f, 0.5f }, &dbTop);
            logMessage ("measured peak: rest " + juce::String (fRest, 0) + " Hz (" + juce::String (dbRest, 1) + " dB), top "
                        + juce::String (fTop, 0) + " Hz (" + juce::String (dbTop, 1) + " dB)");
            expectWithinAbsoluteError (fRest, lo, 120.0);
            expectWithinAbsoluteError (fTop, hi, 500.0);
            expectGreaterThan (dbTop, dbRest + 8.0); // Q (and so the quack) rises with the sweep
        }

        beginTest ("MXR bass voicing sweeps 3.3x lower than the guitar voicing");
        {
            expectWithinAbsoluteError (P::mxrCentreFrequencyHz (P::dutyMax, true) * 3.3, P::mxrCentreFrequencyHz (P::dutyMax, false), 1.0);
            const double fTop = peakGainFreq (Model::mxrBassEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 1.0f, 1.0f, 0.5f }, nullptr);
            logMessage ("bass top peak " + juce::String (fTop, 0) + " Hz");
            expectLessThan (fTop, 1400.0);
            expectGreaterThan (fTop, 500.0);
        }

        beginTest ("playing harder opens the wah (Up) / closes it (Down), for every model");
        {
            const double mxrSoft = tuningAfterNote (Model::mxrEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 0.0f, 0.5f }, 0.004);
            const double mxrHard = tuningAfterNote (Model::mxrEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 0.0f, 0.5f }, 0.2);
            const double dqSoft = tuningAfterNote (Model::doctorQ, { 0.6f, 0.5f, 0.0f, 0.0f, 0.5f }, 0.004);
            const double dqHard = tuningAfterNote (Model::doctorQ, { 0.6f, 0.5f, 0.0f, 0.0f, 0.5f }, 0.2);
            logMessage ("MXR tuning soft " + juce::String (mxrSoft, 2) + " hard " + juce::String (mxrHard, 2)
                        + "; Doctor Q soft " + juce::String (dqSoft, 2) + " hard " + juce::String (dqHard, 2));
            expectGreaterThan (mxrHard, mxrSoft + 0.4);
            expectGreaterThan (dqHard, dqSoft + 0.4);

            const double mxrHardDown = tuningAfterNote (Model::mxrEnvelopeFilter, { 0.5f, 0.3f, 0.4f, 0.46f, 1.0f, 0.5f }, 0.2);
            const double dqHardDown = tuningAfterNote (Model::doctorQ, { 0.6f, 0.5f, 0.0f, 1.0f, 0.5f }, 0.2);
            logMessage ("Down: MXR " + juce::String (mxrHardDown, 2) + ", Doctor Q " + juce::String (dqHardDown, 2));
            expectLessThan (mxrHardDown, mxrHard - 0.4);
            expectLessThan (dqHardDown, dqHard - 0.4);
        }

        beginTest ("Threshold: a higher Threshold needs a harder pick to open the MXR");
        {
            const double easy = tuningAfterNote (Model::mxrEnvelopeFilter, { 0.2f, 0.3f, 0.4f, 0.46f, 0.0f, 0.5f }, 0.02);
            const double hard = tuningAfterNote (Model::mxrEnvelopeFilter, { 0.9f, 0.3f, 0.4f, 0.46f, 0.0f, 0.5f }, 0.02);
            logMessage ("threshold 0.2 -> " + juce::String (easy, 2) + ", 0.9 -> " + juce::String (hard, 2));
            expectGreaterThan (easy, hard + 0.2);
        }

        beginTest ("Doctor Q: Norm is a band-pass, Bass keeps the lows flat with a wah peak on top");
        {
            // Down mode + quiet probe = the top of the sweep.
            const double normLow = quietGainDb (Model::doctorQ, { 0.6f, 0.5f, 0.0f, 1.0f, 0.5f }, 100.0);
            const double bassLow = quietGainDb (Model::doctorQ, { 0.6f, 0.5f, 1.0f, 1.0f, 0.5f }, 100.0);
            double bassPeak = 0.0, normPeak = 0.0;
            peakGainFreq (Model::doctorQ, { 0.6f, 0.5f, 1.0f, 1.0f, 0.5f }, &bassPeak);
            peakGainFreq (Model::doctorQ, { 0.6f, 0.5f, 0.0f, 1.0f, 0.5f }, &normPeak);
            logMessage ("100 Hz: Norm " + juce::String (normLow, 1) + " dB, Bass " + juce::String (bassLow, 1)
                        + " dB; peaks " + juce::String (normPeak, 1) + " / " + juce::String (bassPeak, 1) + " dB");
            expectGreaterThan (bassLow, -4.0);
            expectLessThan (normLow, -20.0);
            expectGreaterThan (bassPeak, bassLow + 8.0);
        }

        beginTest ("Doctor Q sweeps with the transistor's resistance as the closed form says");
        {
            expectGreaterThan (P::doctorQCentreFrequencyHz (150.0), 3000.0);
            expectLessThan (P::doctorQCentreFrequencyHz (2.0e6), 300.0);
            expectGreaterThan (P::doctorQCentreFrequencyHz (10.0e3), P::doctorQCentreFrequencyHz (100.0e3));
        }

        beginTest ("LFO mode moves the wah with no input");
        for (auto m : { Model::mxrEnvelopeFilter, Model::doctorQ })
        {
            auto p = m == Model::doctorQ ? make (m, { 0.6f, 0.5f, 0.0f, 2.0f, 0.8f }) : make (m, { 0.5f, 0.3f, 0.4f, 0.46f, 2.0f, 0.8f });
            juce::AudioBuffer<float> buf (1, 128);
            double lo = 1.0, hi = 0.0;
            for (int b = 0; b < (int) (2.0 * sr / 128.0); ++b)
            {
                buf.clear();
                p->process (buf);
                lo = std::min (lo, p->debugTuning());
                hi = std::max (hi, p->debugTuning());
            }
            logMessage ("LFO tuning range " + juce::String (lo, 2) + " .. " + juce::String (hi, 2));
            expectGreaterThan (hi - lo, 0.5);
        }

        beginTest ("random knob moves, plucked notes and hot bursts stay finite");
        for (auto m : { Model::mxrEnvelopeFilter, Model::mxrBassEnvelopeFilter, Model::doctorQ })
            for (int seed = 31; seed < 33; ++seed)
            {
                P p (m);
                p.prepare (sr, 128, 2);
                expectEquals (runPedalStress (p, 10.0, seed), 0);
            }
    }
};

static EnvelopeAutoWahProcessorTests envelopeAutoWahProcessorTests;

} // namespace openguitarmultifx
