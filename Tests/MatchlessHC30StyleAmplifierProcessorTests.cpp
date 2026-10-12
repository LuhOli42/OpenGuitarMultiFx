#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/MatchlessHC30StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class MatchlessHC30Tests : public juce::UnitTest
{
public:
    MatchlessHC30Tests() : juce::UnitTest ("MatchlessHC30Amplifier", "Effects") {}
    void runTest() override
    {
        using Amp = MatchlessHC30StyleAmplifierProcessor;
        tests::runEL84BoutiqueAmpTests<Amp> (*this, "hc30_volume1", "hc30_treble1",
                                            { 380.5, 154.9, 332.7, 395.3, 5.9 }, "hc30_master", true, 4500.0);
        beginTest ("HC-30 channel and tone-stack controls");
        const double channel1 = tests::measureEL84ControlResponse<Amp> ("hc30_channel", 0.0f, 220.0, 0.02f);
        const double channel2 = tests::measureEL84ControlResponse<Amp> ("hc30_channel", 1.0f, 220.0, 0.02f);
        logMessage ("Channel 1/2 output at 220 Hz: " + juce::String (channel1, 6) + "/" + juce::String (channel2, 6) + " Vrms");
        expect (channel1 > 1.0e-5 && channel2 > 1.0e-5, "both channels are audible");
        const double trebleLow = tests::measureEL84ControlResponse<Amp> ("hc30_treble1", 0.1f, 4500.0, 0.02f, "hc30_channel", 0.0f);
        const double trebleHigh = tests::measureEL84ControlResponse<Amp> ("hc30_treble1", 0.9f, 4500.0, 0.02f, "hc30_channel", 0.0f);
        logMessage ("Channel 1 Treble 4500 Hz: "
                    + juce::String (tests::levelDifferenceDb (trebleHigh, trebleLow), 2) + " dB");
        expect (tests::levelDifferenceDb (trebleHigh, trebleLow) >= 1.5, "Channel 1 Treble adds at least 1.5 dB at 3-6 kHz");
        const double bassLow = tests::measureEL84ControlResponse<Amp> ("hc30_bass1", 0.1f, 100.0, 0.02f, "hc30_channel", 0.0f);
        const double bassHigh = tests::measureEL84ControlResponse<Amp> ("hc30_bass1", 0.9f, 100.0, 0.02f, "hc30_channel", 0.0f);
        logMessage ("Channel 1 Bass 100 Hz: "
                    + juce::String (tests::levelDifferenceDb (bassHigh, bassLow), 2) + " dB");
        expect (tests::levelDifferenceDb (bassHigh, bassLow) >= 1.5, "Channel 1 Bass adds at least 1.5 dB at 100 Hz");
        double previous = tests::measureEL84ControlResponse<Amp> ("hc30_tone2", 0.0f, 4000.0, 0.02f, "hc30_channel", 1.0f);
        const double first = previous;
        for (int position = 1; position <= 5; ++position)
        {
            const double current = tests::measureEL84ControlResponse<Amp> ("hc30_tone2", (float) position, 4000.0,
                                                                           0.02f, "hc30_channel", 1.0f);
            expect (current < previous, "Tone 2 gets progressively darker at 4 kHz");
            previous = current;
        }
        expect (tests::levelDifferenceDb (previous, first) <= -6.0, "Tone 2 positions 0-5 span at least 6 dB");
    }
};
static MatchlessHC30Tests matchlessHC30Tests;
}
