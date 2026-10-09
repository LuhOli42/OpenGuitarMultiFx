#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/Astroverb16StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class Astroverb16Tests : public juce::UnitTest
{
public:
    Astroverb16Tests() : juce::UnitTest ("Astroverb16Amplifier", "Effects") {}
    void runTest() override { tests::runEL84BoutiqueAmpTests<Astroverb16StyleAmplifierProcessor> (*this, "astro16_preamp", "astro16_treble"); }
};
static Astroverb16Tests astroverb16Tests;
}
