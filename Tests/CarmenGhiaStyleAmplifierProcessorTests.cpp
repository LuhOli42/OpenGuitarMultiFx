#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/CarmenGhiaStyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class CarmenGhiaTests : public juce::UnitTest
{
public:
    CarmenGhiaTests() : juce::UnitTest ("CarmenGhiaAmplifier", "Effects") {}
    void runTest() override { tests::runEL84BoutiqueAmpTests<CarmenGhiaStyleAmplifierProcessor> (*this, "cghia_volume", "cghia_tone"); }
};
static CarmenGhiaTests carmenGhiaTests;
}
