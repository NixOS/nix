#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/util/memory-source-accessor.hh"

namespace nix {

TEST(nix_isAllowedURI, http_example_com)
{
    Strings allowed;
    allowed.push_back("http://example.com");

    ASSERT_TRUE(isAllowedURI("http://example.com", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com/foo", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com/foo/", allowed));
    ASSERT_FALSE(isAllowedURI("/", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.co", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.como", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.org", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.org/foo", allowed));
}

TEST(nix_isAllowedURI, http_example_com_foo)
{
    Strings allowed;
    allowed.push_back("http://example.com/foo");

    ASSERT_TRUE(isAllowedURI("http://example.com/foo", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com/foo/", allowed));
    ASSERT_FALSE(isAllowedURI("/foo", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.como", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.org/foo", allowed));
    // Broken?
    // ASSERT_TRUE(isAllowedURI("http://example.com/foo?ok=1", allowed));
}

TEST(nix_isAllowedURI, http)
{
    Strings allowed;
    allowed.push_back("http://");

    ASSERT_TRUE(isAllowedURI("http://", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com/foo", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com/foo/", allowed));
    ASSERT_TRUE(isAllowedURI("http://example.com", allowed));
    ASSERT_FALSE(isAllowedURI("/", allowed));
    ASSERT_FALSE(isAllowedURI("https://", allowed));
    ASSERT_FALSE(isAllowedURI("http:foo", allowed));
}

TEST(nix_isAllowedURI, https)
{
    Strings allowed;
    allowed.push_back("https://");

    ASSERT_TRUE(isAllowedURI("https://example.com", allowed));
    ASSERT_TRUE(isAllowedURI("https://example.com/foo", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com/https:", allowed));
}

TEST(nix_isAllowedURI, absolute_path)
{
    Strings allowed;
    allowed.push_back("/var/evil"); // bad idea

    ASSERT_TRUE(isAllowedURI("/var/evil", allowed));
    ASSERT_TRUE(isAllowedURI("/var/evil/", allowed));
    ASSERT_TRUE(isAllowedURI("/var/evil/foo", allowed));
    ASSERT_TRUE(isAllowedURI("/var/evil/foo/", allowed));
    ASSERT_FALSE(isAllowedURI("/", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evi", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo/", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo/foo", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com/var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com//var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com//var/evil/foo", allowed));
}

TEST(nix_isAllowedURI, file_url)
{
    Strings allowed;
    allowed.push_back("file:///var/evil"); // bad idea

    ASSERT_TRUE(isAllowedURI("file:///var/evil", allowed));
    ASSERT_TRUE(isAllowedURI("file:///var/evil/", allowed));
    ASSERT_TRUE(isAllowedURI("file:///var/evil/foo", allowed));
    ASSERT_TRUE(isAllowedURI("file:///var/evil/foo/", allowed));
    ASSERT_FALSE(isAllowedURI("/", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evi", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo/", allowed));
    ASSERT_FALSE(isAllowedURI("/var/evilo/foo", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com/var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com//var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http://example.com//var/evil/foo", allowed));
    ASSERT_FALSE(isAllowedURI("http://var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http:///var/evil", allowed));
    ASSERT_FALSE(isAllowedURI("http://var/evil/", allowed));
    ASSERT_FALSE(isAllowedURI("file:///var/evi", allowed));
    ASSERT_FALSE(isAllowedURI("file:///var/evilo", allowed));
    ASSERT_FALSE(isAllowedURI("file:///var/evilo/", allowed));
    ASSERT_FALSE(isAllowedURI("file:///var/evilo/foo", allowed));
    ASSERT_FALSE(isAllowedURI("file:///", allowed));
    ASSERT_FALSE(isAllowedURI("file://", allowed));
}

TEST(nix_isAllowedURI, github_all)
{
    Strings allowed;
    allowed.push_back("github:");
    ASSERT_TRUE(isAllowedURI("github:", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar/feat-multi-bar", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar?ref=refs/heads/feat-multi-bar", allowed));
    ASSERT_TRUE(isAllowedURI("github://foo/bar", allowed));
    ASSERT_FALSE(isAllowedURI("https://github:443/foo/bar/archive/master.tar.gz", allowed));
    ASSERT_FALSE(isAllowedURI("file://github:foo/bar/archive/master.tar.gz", allowed));
    ASSERT_FALSE(isAllowedURI("file:///github:foo/bar/archive/master.tar.gz", allowed));
    ASSERT_FALSE(isAllowedURI("github", allowed));
}

TEST(nix_isAllowedURI, github_org)
{
    Strings allowed;
    allowed.push_back("github:foo");
    ASSERT_FALSE(isAllowedURI("github:", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar/feat-multi-bar", allowed));
    ASSERT_TRUE(isAllowedURI("github:foo/bar?ref=refs/heads/feat-multi-bar", allowed));
    ASSERT_FALSE(isAllowedURI("github://foo/bar", allowed));
    ASSERT_FALSE(isAllowedURI("https://github:443/foo/bar/archive/master.tar.gz", allowed));
    ASSERT_FALSE(isAllowedURI("file://github:foo/bar/archive/master.tar.gz", allowed));
    ASSERT_FALSE(isAllowedURI("file:///github:foo/bar/archive/master.tar.gz", allowed));
}

TEST(nix_isAllowedURI, non_scheme_colon)
{
    Strings allowed;
    allowed.push_back("https://foo/bar:");
    ASSERT_TRUE(isAllowedURI("https://foo/bar:", allowed));
    ASSERT_TRUE(isAllowedURI("https://foo/bar:/baz", allowed));
    ASSERT_FALSE(isAllowedURI("https://foo/bar:baz", allowed));
}

class EvalStateTest : public LibExprTest
{};

TEST_F(EvalStateTest, getBuiltins_ok)
{
    auto evaled = maybeThunk("builtins");
    auto & builtins = state.getBuiltins();
    ASSERT_TRUE(builtins.type() == nAttrs);
    ASSERT_EQ(evaled, &builtins);
}

TEST_F(EvalStateTest, getBuiltin_ok)
{
    auto & builtin = state.getBuiltin("toString");
    ASSERT_TRUE(builtin.type() == nFunction);
    // FIXME
    // auto evaled = maybeThunk("builtins.toString");
    // ASSERT_EQ(evaled, &builtin);
    auto & builtin2 = state.getBuiltin("true");
    ASSERT_EQ(state.forceBool(builtin2, noPos, "in unit test"), true);
}

TEST_F(EvalStateTest, getBuiltin_fail)
{
    ASSERT_THROW(state.getBuiltin("nonexistent"), EvalError);
}

class PureEvalTest : public LibExprTest
{
public:
    PureEvalTest()
        : LibExprTest(openStore("dummy://", {{"read-only", "false"}}), [](bool & readOnlyMode) {
            EvalSettings settings{readOnlyMode};
            settings.pureEval = true;
            settings.restrictEval = true;
            return settings;
        })
    {
    }
};

TEST_F(PureEvalTest, pathExists)
{
    ASSERT_THAT(eval("builtins.pathExists /."), IsFalse());
    ASSERT_THAT(eval("builtins.pathExists /nix"), IsFalse());
    ASSERT_THAT(eval("builtins.pathExists /nix/store"), IsFalse());

    {
        std::string contents = "Lorem ipsum";

        StringSource s{contents};
        auto path = state.store->addToStoreFromDump(
            s, "source", FileSerialisationMethod::Flat, ContentAddressMethod::Raw::Text, HashAlgorithm::SHA256);
        auto printed = store->printStorePath(path);

        ASSERT_THROW(eval(fmt("builtins.readFile %s", printed)), RestrictedPathError);
        ASSERT_THAT(eval(fmt("builtins.pathExists %s", printed)), IsFalse());

        ASSERT_THROW(eval("builtins.readDir /."), RestrictedPathError);
        state.allowPath(path); // FIXME: This shouldn't behave this way.
        ASSERT_THAT(eval("builtins.readDir /."), IsAttrsOfSize(0));
    }
}

} // namespace nix
