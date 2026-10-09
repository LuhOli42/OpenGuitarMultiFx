#pragma once

#include "EL84BoutiqueAmpCore.h"

namespace openguitarmultifx
{

class CarmenGhiaStyleAmplifierProcessor final : public EL84BoutiqueAmpCore
{
public:
    CarmenGhiaStyleAmplifierProcessor();
    static inline bool reducedOrder = false;
};

}
