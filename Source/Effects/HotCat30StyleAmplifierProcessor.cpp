#include "HotCat30StyleAmplifierProcessor.h"

namespace openguitarmultifx
{
namespace
{
    KorenTriode::Parameters triode12AX7() { return {}; }
    KorenPentode::Parameters pentodeEF86()
    {
        KorenPentode::Parameters p;
        p.mu = 34.9; p.ex = 1.35; p.kg1 = 2648.1; p.kg2 = 4500.0; p.kp = 222.06; p.kvb = 4.7;
        return p;
    }
    KorenPentode::Parameters pentodeEL34()
    {
        // The real Hot Cat 30 runs a pair of cathode-biased EL34s (V5/V6),
        // not the four EL84s this slot was originally filled with.
        KorenPentode::Parameters p;
        p.mu = 8.11; p.ex = 1.5; p.kg1 = 1201.0; p.kg2 = 3720.0; p.kp = 100.0; p.kvb = 24.0;
        return p;
    }
    KorenPentode::Parameters pentodeEL34Pair()
    {
        auto p = pentodeEL34();
        p.kg1 /= 2.0; p.kg2 /= 2.0; p.grid.Gg *= 2.0; p.arcResistance /= 2.0;
        return p;
    }
}
HotCat30StyleAmplifierProcessor::HotCat30StyleAmplifierProcessor()
    : EL84BoutiqueAmpCore (Model::hotCat30,
        { triode12AX7(), pentodeEF86(), pentodeEL34(), pentodeEL34Pair() }, reducedOrder, "Hot Cat 30")
{
}
}
