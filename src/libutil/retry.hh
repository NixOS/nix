#pragma once

#include "logging.hh"

#include <functional>
#include <cmath>
#include <random>
#include <thread>

namespace nix {

inline unsigned int retrySleepTime(unsigned int attempt)
{
    std::random_device rd;
    std::mt19937 mt19937;
    return 250.0 * std::pow(2.0f,
        attempt - 1 + std::uniform_real_distribution<>(0.0, 0.5)(mt19937));
}

template<typename C>
C retry(unsigned int attempts, std::function<C()> && f)
{
    unsigned int attempt = 0;
    while (true) {
        try {
            return f();
        } catch (BaseError & e) {
            ++attempt;
            if (attempt >= attempts || !e.isTransient())
                throw;
            auto ms = retrySleepTime(attempt);
            warn("%s; retrying in %d ms", e.what(), ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }
}

}
