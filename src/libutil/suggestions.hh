#pragma once

#include "comparator.hh"
#include "types.hh"
#include <set>

namespace nix {

/**
 * A potential suggestion for the cli interface.
 */
class Suggestion {
public:
    int distance; // The smaller the better
    std::string suggestion;

    std::string pretty_print() const;

    GENERATE_CMP(Suggestion, me->distance, me->suggestion)
};

class Suggestions {
public:
    std::set<Suggestion> suggestions;

    std::string pretty_print() const;

    Suggestions trim(
        int limit = 5,
        int maxDistance = 2
    ) const;

    static Suggestions bestMatches (
        std::set<std::string> allMatches,
        std::string query
    );

    Suggestions& operator+=(const Suggestions & other);
};

}
