#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/CarmenGhiaStyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class CarmenGhiaTests : public juce::UnitTest
{
public:
    CarmenGhiaTests() : juce::UnitTest ("CarmenGhiaAmplifier", "Effects") {}
    void runTest() override
    {
        using Amp = CarmenGhiaStyleAmplifierProcessor;
        tests::runEL84BoutiqueAmpTests<Amp> (*this, "cghia_volume", "cghia_tone",
                                            { 340.0, 196.4, 332.7, 394.3, 14.9 }, "cghia_master");
        beginTest ("Carmen Ghia tone and Master");
        const double toneLow = tests::measureEL84ControlResponse<Amp> ("cghia_tone", 0.1f, 4000.0, 0.002f);
        const double toneHigh = tests::measureEL84ControlResponse<Amp> ("cghia_tone", 0.9f, 4000.0, 0.002f);
        expect (std::abs (tests::levelDifferenceDb (toneHigh, toneLow)) >= 3.0, "Tone changes 4 kHz by at least 3 dB");
        const double fullCW = tests::measureEL84ControlResponse<Amp> ("cghia_master", 1.0f, 220.0, 0.02f, nullptr, 0.5f, true);
        const double noMaster = tests::measureEL84ControlResponse<Amp> ("", 0.0f, 220.0, 0.02f, nullptr, 0.5f, true);
        expect (std::abs (tests::levelDifferenceDb (fullCW, noMaster)) <= 0.5,
                "Master full CW is bypass-equivalent in reduced mode");
        const double fullQuiet = tests::measureEL84ControlResponse<Amp> ("cghia_master", 0.2f, 220.0, 0.02f);
        const double fullLoud = tests::measureEL84ControlResponse<Amp> ("cghia_master", 1.0f, 220.0, 0.02f);
        expect (tests::levelDifferenceDb (fullQuiet, fullLoud) <= -6.0,
                "post-PI Master attenuates the full netlist by at least 6 dB");
    }
};
static CarmenGhiaTests carmenGhiaTests;
}
