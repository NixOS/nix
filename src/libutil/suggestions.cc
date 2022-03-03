#include "suggestions.hh"
#include "ansicolor.hh"
#include "util.hh"
#include <algorithm>

namespace nix {

/**
 * Return `some(distance)` where distance is an integer representing some
 * notion of distance between both arguments.
 *
 * If the distance is too big, return none
 */
int distanceBetween(std::string_view first, std::string_view second)
{
    // Levenshtein distance.
    // Implementation borrowed from
    // https://en.wikipedia.org/wiki/Levenshtein_distance#Iterative_with_two_matrix_rows

    int m = first.size();
    int n = second.size();

    auto v0 = std::vector<int>(n+1);
    auto v1 = std::vector<int>(n+1);

    for (auto i = 0; i <= n; i++)
        v0[i] = i;

    for (auto i = 0; i < m; i++) {
        v1[0] = i+1;

        for (auto j = 0; j < n; j++) {
            auto deletionCost = v0[j+1] + 1;
            auto insertionCost = v1[j] + 1;
            auto substitutionCost = first[i] == second[j] ? v0[j] : v0[j] + 1;
            v1[j+1] = std::min({deletionCost, insertionCost, substitutionCost});
        }

        std::swap(v0, v1);
    }

    return v0[n];
}

Suggestions Suggestions::bestMatches (
    std::set<std::string> allMatches,
    std::string query)
{
    std::set<Suggestion> res;
    for (const auto & possibleMatch : allMatches) {
        res.insert(Suggestion {
            .distance = distanceBetween(query, possibleMatch),
            .suggestion = possibleMatch,
        });
    }
    return Suggestions { res };
}

Suggestions Suggestions::trim(int limit, int maxDistance) const
{
    std::set<Suggestion> res;

    int count = 0;

    for (auto & elt : suggestions) {
        if (count >= limit || elt.distance >= maxDistance)
            break;
        count++;
        res.insert(elt);
    }

    return Suggestions{res};
}

std::string Suggestion::pretty_print() const
{
    return ANSI_WARNING + filterANSIEscapes(suggestion) + ANSI_NORMAL;
}

std::string Suggestions::pretty_print() const
{
    switch (suggestions.size()) {
        case 0:
            return "";
        case 1:
            return suggestions.begin()->pretty_print();
        default: {
            std::string res = "one of ";
            auto iter = suggestions.begin();
            res += iter->pretty_print(); // Iter can’t be end() because the container isn’t null
            iter++;
            auto last = suggestions.end(); last--;
            for ( ; iter != suggestions.end() ; iter++) {
                res += (iter == last) ? " or " : ", ";
                res += iter->pretty_print();
            }
            return res;
        }
    }
}

Suggestions & Suggestions::operator+=(const Suggestions & other)
{
    suggestions.insert(
            other.suggestions.begin(),
            other.suggestions.end()
    );
    return *this;
}

}
