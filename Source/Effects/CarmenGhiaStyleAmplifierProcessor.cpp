#include "CarmenGhiaStyleAmplifierProcessor.h"

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
    KorenPentode::Parameters pentodeEL84()
    {
        KorenPentode::Parameters p;
        p.mu = 16.0; p.ex = 1.35; p.kg1 = 570.0; p.kg2 = 4200.0; p.kp = 50.0; p.kvb = 24.0;
        return p;
    }
    KorenPentode::Parameters pentodeEL84Pair()
    {
        auto p = pentodeEL84();
        p.kg1 = 570.0 / 2.0 * 6.0; p.kg2 /= 2.0; p.grid.Gg *= 2.0; p.arcResistance /= 2.0;
        return p;
    }
}
CarmenGhiaStyleAmplifierProcessor::CarmenGhiaStyleAmplifierProcessor()
    : EL84BoutiqueAmpCore (Model::carmenGhia,
        { triode12AX7(), pentodeEF86(), pentodeEL84(), pentodeEL84Pair() }, reducedOrder, "Carmen Ghia-Style Amplifier")
{
}
}
