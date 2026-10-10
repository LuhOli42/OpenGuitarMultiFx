#pragma once

#include "EL84BoutiqueAmpCore.h"

namespace openguitarmultifx
{

class HotCat30StyleAmplifierProcessor final : public EL84BoutiqueAmpCore
{
public:
    HotCat30StyleAmplifierProcessor();
    static inline bool reducedOrder = false;
};

}
