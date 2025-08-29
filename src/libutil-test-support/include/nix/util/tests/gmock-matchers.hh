#pragma once
///@file

#include "nix/util/terminal.hh"
#include <gmock/gmock.h>

namespace nix::testing {

namespace internal {

/**
 * GMock matcher that matches substring while stripping off all ANSI escapes.
 * Useful for checking exceptions messages in unit tests.
 */
class HasSubstrIgnoreANSIMatcher
{
public:
    explicit HasSubstrIgnoreANSIMatcher(std::string substring)
        : substring(std::move(substring))
    {
    }

    bool MatchAndExplain(const char * s, ::testing::MatchResultListener * listener) const
    {
        return s != nullptr && MatchAndExplain(std::string(s), listener);
    }

    template<typename MatcheeStringType>
    bool MatchAndExplain(const MatcheeStringType & s, [[maybe_unused]] ::testing::MatchResultListener * listener) const
    {
        return filterANSIEscapes(s, /*filterAll=*/true).find(substring) != substring.npos;
    }

    void DescribeTo(::std::ostream * os) const
    {
        *os << "has substring " << substring;
    }

    void DescribeNegationTo(::std::ostream * os) const
    {
        *os << "has no substring " << substring;
    }

private:
    std::string substring;
};

} // namespace internal

inline ::testing::PolymorphicMatcher<internal::HasSubstrIgnoreANSIMatcher>
HasSubstrIgnoreANSIMatcher(const std::string & substring)
{
    return ::testing::MakePolymorphicMatcher(internal::HasSubstrIgnoreANSIMatcher(substring));
}

} // namespace nix::testing
