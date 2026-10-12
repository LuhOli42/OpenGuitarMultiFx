#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/HotCat30StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class HotCat30Tests : public juce::UnitTest
{
public:
    HotCat30Tests() : juce::UnitTest ("HotCat30Amplifier", "Effects") {}
    void runTest() override
    {
        using Amp = HotCat30StyleAmplifierProcessor;
        tests::runEL84BoutiqueAmpTests<Amp> (*this, "hotcat30_gain", "hotcat30_treble",
                                            { 380.5, 196.4, 332.7, 395.3, 5.9 }, "hotcat30_master");
        beginTest ("Hot Cat tone controls");
        const auto difference = [] (const char* id, double frequency)
        {
            const double low = tests::measureEL84ControlResponse<Amp> (id, 0.1f, frequency, 0.002f);
            const double high = tests::measureEL84ControlResponse<Amp> (id, 0.9f, frequency, 0.002f);
            return tests::levelDifferenceDb (high, low);
        };
        const double bass = difference ("hotcat30_bass", 100.0);
        const double mid = difference ("hotcat30_mid", 1000.0);
        const double treble = difference ("hotcat30_treble", 4500.0);
        logMessage ("Bass 100 Hz " + juce::String (bass, 2) + " dB; Mid 1 kHz "
                    + juce::String (mid, 2) + " dB; Treble 4.5 kHz " + juce::String (treble, 2) + " dB");
        expect (bass >= 1.5, "Bass increases its low-frequency band");
        expect (mid >= 1.5, "Mid increases its mid-frequency band");
        expect (treble >= 1.5, "Treble increases its high-frequency band");
    }
};
static HotCat30Tests hotCat30Tests;
}
