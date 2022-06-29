#include "git.hh"
#include <gtest/gtest.h>

namespace nix {

TEST(GitLsRemote, parseSymrefLineWithReference)
{
    auto line = "ref: refs/head/main	HEAD";
    auto res = git::parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, git::LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, "HEAD");
}

TEST(GitLsRemote, parseSymrefLineWithNoReference)
{
    auto line = "ref: refs/head/main";
    auto res = git::parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, git::LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, std::nullopt);
}

TEST(GitLsRemote, parseObjectRefLine)
{
    auto line = "abc123	refs/head/main";
    auto res = git::parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, git::LsRemoteRefLine::Kind::Object);
    ASSERT_EQ(res->target, "abc123");
    ASSERT_EQ(res->reference, "refs/head/main");
}
}
