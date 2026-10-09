#pragma once

#include "EL84BoutiqueAmpCore.h"

namespace openguitarmultifx
{

class MatchlessHC30StyleAmplifierProcessor final : public EL84BoutiqueAmpCore
{
public:
    MatchlessHC30StyleAmplifierProcessor();
    static inline bool reducedOrder = false;
};

}
