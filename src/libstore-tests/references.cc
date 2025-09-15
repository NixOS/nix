#include "nix/store/references.hh"

#include <gtest/gtest.h>

namespace nix {

struct RewriteParams
{
    std::string originalString, finalString;
    StringMap rewrites;

    friend std::ostream & operator<<(std::ostream & os, const RewriteParams & bar)
    {
        StringSet strRewrites;
        for (auto & [from, to] : bar.rewrites)
            strRewrites.insert(from + "->" + to);
        return os << "OriginalString: " << bar.originalString << std::endl
                  << "Rewrites: " << dropEmptyInitThenConcatStringsSep(",", strRewrites) << std::endl
                  << "Expected result: " << bar.finalString;
    }
};

class RewriteTest : public ::testing::TestWithParam<RewriteParams>
{};

TEST_P(RewriteTest, IdentityRewriteIsIdentity)
{
    RewriteParams param = GetParam();
    StringSink rewritten;
    auto rewriter = RewritingSink(param.rewrites, rewritten);
    rewriter(param.originalString);
    rewriter.flush();
    ASSERT_EQ(rewritten.s, param.finalString);
}

INSTANTIATE_TEST_CASE_P(
    references,
    RewriteTest,
    ::testing::Values(
        RewriteParams{"foooo", "baroo", {{"foo", "bar"}, {"bar", "baz"}}},
        RewriteParams{"foooo", "bazoo", {{"fou", "bar"}, {"foo", "baz"}}},
        RewriteParams{"foooo", "foooo", {}}));

TEST(references, scan)
{
    std::string hash1 = "dc04vv14dak1c1r48qa0m23vr9jy8sm0";
    std::string hash2 = "zc842j0rz61mjsp3h3wp5ly71ak6qgdn";

    {
        RefScanSink scanner(StringSet{hash1});
        auto s = "foobar";
        scanner(s);
        ASSERT_EQ(scanner.getResult(), StringSet{});
    }

    {
        RefScanSink scanner(StringSet{hash1});
        auto s = "foobar" + hash1 + "xyzzy";
        scanner(s);
        ASSERT_EQ(scanner.getResult(), StringSet{hash1});
    }

    {
        RefScanSink scanner(StringSet{hash1, hash2});
        auto s = "foobar" + hash1 + "xyzzy" + hash2;
        scanner(((std::string_view) s).substr(0, 10));
        scanner(((std::string_view) s).substr(10, 5));
        scanner(((std::string_view) s).substr(15, 5));
        scanner(((std::string_view) s).substr(20));
        ASSERT_EQ(scanner.getResult(), StringSet({hash1, hash2}));
    }

    {
        RefScanSink scanner(StringSet{hash1, hash2});
        auto s = "foobar" + hash1 + "xyzzy" + hash2;
        for (auto & i : s)
            scanner(std::string(1, i));
        ASSERT_EQ(scanner.getResult(), StringSet({hash1, hash2}));
    }
}

} // namespace nix
