#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/Astroverb16StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class Astroverb16Tests : public juce::UnitTest
{
public:
    Astroverb16Tests() : juce::UnitTest ("Astroverb16Amplifier", "Effects") {}
    void runTest() override
    {
        using Amp = Astroverb16StyleAmplifierProcessor;
        tests::runEL84BoutiqueAmpTests<Amp> (*this, "astro16_preamp", "astro16_treble",
                                            { 380.5, 153.1, 332.2, 394.3, 14.9 }, "astro16_volume");
        beginTest ("Astroverb tone and Presence controls");
        const auto difference = [] (const char* id, double frequency, bool reducedOrder = false, float input = 0.02f)
        {
            const double low = tests::measureEL84ControlResponse<Amp> (id, 0.1f, frequency, input,
                                                                        nullptr, 0.5f, reducedOrder);
            const double high = tests::measureEL84ControlResponse<Amp> (id, 0.9f, frequency, input,
                                                                         nullptr, 0.5f, reducedOrder);
            return tests::levelDifferenceDb (high, low);
        };
        const double bass = difference ("astro16_bass", 100.0, false, 0.002f);
        const double mid = difference ("astro16_mid", 1000.0, false, 0.002f);
        const double treble = difference ("astro16_treble", 4500.0, false, 0.002f);
        const double presenceFull = difference ("astro16_presence", 4500.0, false, 0.002f);
        const double presenceReduced = difference ("astro16_presence", 4500.0, true, 0.002f);
        logMessage ("Bass 100 Hz " + juce::String (bass, 2) + " dB; Middle 1 kHz "
                    + juce::String (mid, 2) + " dB; Treble 4.5 kHz " + juce::String (treble, 2)
                    + " dB; Presence full/reduced 4.5 kHz " + juce::String (presenceFull, 2) + "/"
                    + juce::String (presenceReduced, 2) + " dB");
        expect (bass >= 1.5, "Bass increases its low-frequency band");
        expect (mid >= 1.5, "Middle increases its mid-frequency band");
        expect (treble >= 1.5, "Treble increases its high-frequency band");
        expect (presenceFull >= 1.5, "Presence increases 3-6 kHz in full-netlist mode");
        expect (presenceReduced >= 1.5, "Presence increases 3-6 kHz in reduced-order mode");
    }
};
static Astroverb16Tests astroverb16Tests;
}
