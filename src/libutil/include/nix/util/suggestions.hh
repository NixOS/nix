#pragma once
///@file

#include "nix/util/types.hh"
#include <set>

namespace nix {

int levenshteinDistance(std::string_view first, std::string_view second);

/**
 * A potential suggestion for the cli interface.
 */
class Suggestion
{
public:
    /// The smaller the better
    int distance;
    std::string suggestion;

    std::string to_string() const;

    bool operator==(const Suggestion &) const = default;
    auto operator<=>(const Suggestion &) const = default;
};

class Suggestions
{
public:
    std::set<Suggestion> suggestions;

    std::string to_string() const;

    Suggestions trim(int limit = 5, int maxDistance = 2) const;

    static Suggestions bestMatches(const StringSet & allMatches, std::string_view query);

    Suggestions & operator+=(const Suggestions & other);
};

std::ostream & operator<<(std::ostream & str, const Suggestion &);
std::ostream & operator<<(std::ostream & str, const Suggestions &);

/**
 * Either a value of type `T`, or some suggestions
 */
template<typename T>
class OrSuggestions
{
public:
    using Raw = std::variant<T, Suggestions>;

    Raw raw;

    T * operator->()
    {
        return &**this;
    }

    T & operator*()
    {
        return std::get<T>(raw);
    }

    operator bool() const noexcept
    {
        return std::holds_alternative<T>(raw);
    }

    OrSuggestions(T t)
        : raw(t)
    {
    }

    OrSuggestions()
        : raw(Suggestions{})
    {
    }

    static OrSuggestions<T> failed(const Suggestions & s)
    {
        auto res = OrSuggestions<T>();
        res.raw = s;
        return res;
    }

    static OrSuggestions<T> failed()
    {
        return OrSuggestions<T>::failed(Suggestions{});
    }

    const Suggestions & getSuggestions()
    {
        static Suggestions noSuggestions;
        if (const auto & suggestions = std::get_if<Suggestions>(&raw))
            return *suggestions;
        else
            return noSuggestions;
    }
};

} // namespace nix
