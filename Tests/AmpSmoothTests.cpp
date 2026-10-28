#include "EffectRegistry.h"
#include "Effects/OutputTrimEffect.h"
#include "Effects/TubeAmpCommon.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The "Smooth" knob every amp gets from OutputTrimEffect (limit = true): absent on pedals, 0 by default, and at 0 the
    amp's output is bit-identical to the unfiltered one -- the shelf only ever runs while the knob is open or ramping. */
class AmpSmoothTests : public juce::UnitTest
{
public:
    AmpSmoothTests() : juce::UnitTest ("AmpSmooth", "Effects") {}

    struct Passthrough : EffectProcessor
    {
        Passthrough() : group ("pass", "Pass", "|") {}
        void prepare (double, int, int) override {}
        void process (juce::AudioBuffer<float>&) override {}
        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return &group; }
        const char* getName() const override { return "Passthrough"; }
        juce::AudioProcessorParameterGroup group;
    };

    static juce::AudioParameterFloat* findSmooth (EffectProcessor& p)
    {
        if (auto* group = p.getParameters())
            for (auto* param : group->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param))
                    if (f->paramID == "smooth")
                        return f;
        return nullptr;
    }

    static constexpr double sr = 48000.0;

    static double responseDb (float smooth, double freq)
    {
        OutputTrimEffect amp (std::make_unique<Passthrough>(), 0.0f, true);
        amp.prepare (sr, 1, 1);
        *findSmooth (amp) = smooth;
        return 20.0 * std::log10 (probeSine (amp, {}, freq, 0.01, sr, 0.2, 0.1).out / 0.01);
    }

    void runTest() override
    {
        beginTest ("every amp has a Smooth knob at 0, pedals don't");
        {
            EffectRegistry registry;
            registerBuiltInEffects (registry);
            int amps = 0;
            for (const auto& key : registry.getRegisteredNames())
            {
                auto fx = registry.create (key);
                auto* smooth = fx != nullptr ? findSmooth (*fx) : nullptr;
                if (registry.categoryForKey (key) == EffectRegistry::Category::amps)
                {
                    ++amps;
                    expect (smooth != nullptr, key + " has no Smooth knob");
                    if (smooth != nullptr)
                    {
                        expectEquals (smooth->get(), 0.0f, key);
                        bool onAPage = false;
                        for (const auto& page : fx->getParameterPages())
                            onAPage = onAPage || std::find (page.begin(), page.end(), smooth) != page.end();
                        expect (onAPage, key + ": Smooth is on no knob page");
                        *smooth = 0.5f;
                        auto state = fx->getState();
                        auto fresh = registry.create (key);
                        fresh->setState (*state);
                        expectWithinAbsoluteError (findSmooth (*fresh)->get(), 0.5f, 1.0e-6f, key + " preset round trip");
                    }
                }
                else
                {
                    expect (smooth == nullptr, key + " is not an amp but has a Smooth knob");
                }
            }
            expectGreaterThan (amps, 30);
        }

        beginTest ("Smooth = 0 is bit-identical to no filter");
        {
            OutputTrimEffect amp (std::make_unique<Passthrough>(), 0.0f, true);
            amp.prepare (sr, 64, 2);
            juce::Random rng (7);
            juce::AudioBuffer<float> buf (2, 64);
            for (int block = 0; block < 50; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
                juce::AudioBuffer<float> ref (buf);
                amp.process (buf);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        if (! juce::exactlyEqual (buf.getSample (ch, i), (float) tubeamp::outputCeiling (ref.getSample (ch, i))))
                            return expect (false, "Smooth = 0 changed the signal");
            }
        }

        beginTest ("Smooth = 1 is a gentle -6 dB high shelf around 5 kHz");
        {
            expectWithinAbsoluteError (responseDb (1.0f, 100.0), 0.0, 0.05);
            expectWithinAbsoluteError (responseDb (1.0f, 1000.0), 0.0, 0.5);
            expectWithinAbsoluteError (responseDb (1.0f, 5000.0), -3.0, 0.6);
            expectWithinAbsoluteError (responseDb (1.0f, 15000.0), -6.0, 0.4);
            expectWithinAbsoluteError (responseDb (0.5f, 15000.0), -3.0, 0.3);
        }

        beginTest ("turning Smooth on and off ramps without a step and returns to bit-exact");
        {
            OutputTrimEffect amp (std::make_unique<Passthrough>(), 0.0f, true);
            amp.prepare (sr, 32, 1);
            auto* smooth = findSmooth (amp);
            juce::AudioBuffer<float> buf (1, 32);
            double phase = 0.0, prev = 0.0, maxJump = 0.0;
            auto run = [&] (int blocks, bool checkExact)
            {
                bool exact = true;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 32; ++i)
                    {
                        buf.setSample (0, i, (float) (0.3 * std::sin (phase)));
                        phase += juce::MathConstants<double>::twoPi * 8000.0 / sr;
                    }
                    juce::AudioBuffer<float> ref (buf);
                    amp.process (buf);
                    for (int i = 0; i < 32; ++i)
                    {
                        const double y = buf.getSample (0, i);
                        maxJump = juce::jmax (maxJump, std::abs (y - prev));
                        prev = y;
                        exact = exact && juce::exactlyEqual (buf.getSample (0, i), (float) tubeamp::outputCeiling (ref.getSample (0, i)));
                    }
                }
                return ! checkExact || exact;
            };
            run (20, false);
            const double cleanJump = maxJump;
            *smooth = 1.0f;
            run (100, false);
            *smooth = 0.0f;
            run (50, false);
            expect (run (20, true), "not bit-exact again after the knob went back to 0");
            expectLessThan (maxJump, cleanJump * 1.05, "knob moves made a step bigger than the signal's own slope");
        }
    }
};

static AmpSmoothTests ampSmoothTests;

} // namespace openguitarmultifx
