#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/HotCat30StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class HotCat30Tests : public juce::UnitTest
{
public:
    HotCat30Tests() : juce::UnitTest ("HotCat30Amplifier", "Effects") {}
    void runTest() override { tests::runEL84BoutiqueAmpTests<HotCat30StyleAmplifierProcessor> (*this, "hotcat30_gain", "hotcat30_treble"); }
};
static HotCat30Tests hotCat30Tests;
}
