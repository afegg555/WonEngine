#include "Random.h"

#include <random>

namespace won::random
{
    uint32 GetRandomSeed()
    {
        const uint32 seed = static_cast<uint32>(std::random_device{}());
        return seed != 0 ? seed : 1u;
    }
}
