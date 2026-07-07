#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/source-accessor.hh"
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

/**
 * Matches a callable that throws `SysError` whose `errNo` equals `expected`.
 *
 * Example:
 *
 *     EXPECT_THAT([&]{ openFile("nope"); }, ThrowsSysError(ENOENT));
 */
inline auto ThrowsSysError(int expected)
{
    return ::testing::Throws<SysError>(::testing::Field(&SysError::errNo, expected));
}

MATCHER_P2(HasContents, path, expected, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tRegular) {
        *result_listener << arg->showPath(path) << " is not a regular file";
        return false;
    }
    auto actual = arg->readFile(path);
    if (actual != expected) {
        *result_listener << arg->showPath(path) << " has contents " << ::testing::PrintToString(actual);
        return false;
    }
    return true;
}

MATCHER_P2(HasSymlink, path, target, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tSymlink) {
        *result_listener << arg->showPath(path) << " is not a symlink";
        return false;
    }
    auto actual = arg->readLink(path);
    if (actual != target) {
        *result_listener << arg->showPath(path) << " points to " << ::testing::PrintToString(actual);
        return false;
    }
    return true;
}

MATCHER_P2(HasDirectory, path, dirents, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tDirectory) {
        *result_listener << arg->showPath(path) << " is not a directory";
        return false;
    }
    auto actual = arg->readDirectory(path);
    std::set<std::string> actualKeys, expectedKeys(dirents.begin(), dirents.end());
    for (auto & [k, _] : actual)
        actualKeys.insert(k);
    if (actualKeys != expectedKeys) {
        *result_listener << arg->showPath(path) << " has entries " << ::testing::PrintToString(actualKeys);
        return false;
    }
    return true;
}

} // namespace nix::testing
