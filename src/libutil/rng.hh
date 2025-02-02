#pragma once
/*! \file
 *  \brief Psuedo-random number generators class for easier use of C++'s random number generation facilities.
 */

#include <random>
#include <limits>

namespace nix {

// Inspired by the book "A Tour of C++, Third Edition" (ISBN-10 0136816487)
template<typename T, typename Distribution, typename Engine>
struct RandomNumberGenerator
{
public:
    using limits = std::numeric_limits<T>;
    RandomNumberGenerator(T low = limits::min(), T high = limits::max())
        : engine(std::random_device{}())
        , dist(low, high){};
    RandomNumberGenerator(std::seed_seq seed, T low, T high)
        : engine(seed)
        , dist(low, high){};
    T operator()()
    {
        return dist(engine);
    }
    void seed(int s)
    {
        engine.seed(s);
    }
private:
    Engine engine;
    Distribution dist;
};

using RandomIntGenerator = RandomNumberGenerator<int, std::uniform_int_distribution<int>, std::default_random_engine>;
using RandomFloatGenerator =
    RandomNumberGenerator<float, std::uniform_real_distribution<float>, std::default_random_engine>;

} // namespace nix
