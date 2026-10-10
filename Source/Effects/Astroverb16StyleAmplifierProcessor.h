#pragma once

#include "EL84BoutiqueAmpCore.h"

namespace openguitarmultifx
{

class Astroverb16StyleAmplifierProcessor final : public EL84BoutiqueAmpCore
{
public:
    Astroverb16StyleAmplifierProcessor();
    static inline bool reducedOrder = false;
};

}
