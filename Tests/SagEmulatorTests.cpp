#include "Effects/NAMProcessor.h"
#include "Effects/SagEmulator.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

class SagEmulatorTests : public juce::UnitTest
{
public:
    SagEmulatorTests() : juce::UnitTest ("SagEmulator", "Effects") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;

        beginTest ("amount 0 leaves the signal bit-exact");
        {
            SagEmulator sag;
            sag.prepare (sr);
            std::vector<float> x (4800), y;
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (0.9 * std::sin (0.07 * (double) i));
            y = x;
            sag.process (y.data(), (int) y.size(), 0.0f);
            expect (x == y);
        }

        beginTest ("a loud passage squashes itself, then recovers");
        {
            SagEmulator sag;
            sag.prepare (sr);
            const auto burst = [&] (double level, int samples, float amount)
            {
                std::vector<float> x ((size_t) samples);
                for (int i = 0; i < samples; ++i)
                    x[(size_t) i] = (float) (level * std::sin (0.11 * (double) i));
                sag.process (x.data(), samples, amount);
                double sum = 0.0;
                for (int i = samples * 3 / 4; i < samples; ++i)
                    sum += (double) x[(size_t) i] * x[(size_t) i];
                return std::sqrt (sum / (samples / 4.0)) / (level * 0.7071);
            };
            const double quiet = burst (0.05, 4800, 1.0f);            // little draw: almost no droop
            const double loudEarly = burst (0.8, 480, 1.0f);           // 10 ms into the burst
            const double loudLate = burst (0.8, 9600, 1.0f);           // 200 ms later
            const double after = burst (0.05, 480, 1.0f);              // quiet again right after: still squashed
            const double recovered = burst (0.05, 48000, 1.0f);        // a second later: back to normal
            logMessage ("gain quiet " + juce::String (quiet, 3) + ", loud early " + juce::String (loudEarly, 3) + ", loud late " + juce::String (loudLate, 3) + ", right after " + juce::String (after, 3) + ", recovered " + juce::String (recovered, 3));
            expect (quiet > 0.93, "a quiet signal is left alone");
            expect (loudLate < 0.75, "a loud one droops");
            expect (loudLate < loudEarly, "the droop builds over tens of milliseconds");
            expect (after < quiet - 0.03, "and does not vanish the instant the signal does");
            expect (recovered > 0.93, "it recovers");
        }

        beginTest ("the neural amp has a Sag control on page 2, the neural pedal does not");
        {
            NAMProcessor amp ("Neural Amp"), ampCab ("Neural Amp + Cab"), pedal ("Neural Pedal");
            expectEquals ((int) amp.getParameterPages().size(), 2);
            expectEquals ((int) ampCab.getParameterPages().size(), 2);
            expectEquals ((int) pedal.getParameterPages().size(), 1);
        }
    }
};

static SagEmulatorTests sagEmulatorTests;

} // namespace openguitarmultifx
