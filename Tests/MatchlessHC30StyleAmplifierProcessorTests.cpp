#include "EL84BoutiqueAmpTestCommon.h"
#include "Effects/MatchlessHC30StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
class MatchlessHC30Tests : public juce::UnitTest
{
public:
    MatchlessHC30Tests() : juce::UnitTest ("MatchlessHC30Amplifier", "Effects") {}
    void runTest() override { tests::runEL84BoutiqueAmpTests<MatchlessHC30StyleAmplifierProcessor> (*this, "hc30_volume2", "hc30_tone2"); }
};
static MatchlessHC30Tests matchlessHC30Tests;
}
