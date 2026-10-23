#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/util/file-system.hh"
#include "nix/store/store-reference.hh"

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

using nlohmann::json;

class StoreReferenceTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "store-reference";

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (testStem + ".txt");
    }
};

#define URI_TEST_READ(STEM, OBJ)                                \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_from_uri)      \
    {                                                           \
        readTest(#STEM, ([&](const auto & encoded) {            \
                     StoreReference expected = OBJ;             \
                     auto got = StoreReference::parse(encoded); \
                     ASSERT_EQ(got, expected);                  \
                 }));                                           \
    }

#define URI_TEST_WRITE(STEM, OBJ)                                                               \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_to_uri)                                        \
    {                                                                                           \
        writeTest(                                                                              \
            #STEM,                                                                              \
            [&]() -> StoreReference { return OBJ; },                                            \
            [](const auto & file) { return StoreReference::parse(readFile(file)); },            \
            [](const auto & file, const auto & got) { return writeFile(file, got.render()); }); \
    }

#define URI_TEST(STEM, OBJ)  \
    URI_TEST_READ(STEM, OBJ) \
    URI_TEST_WRITE(STEM, OBJ)

URI_TEST(
    auto,
    (StoreReference{
        .variant = StoreReference::Auto{},
        .params = {},
    }))

URI_TEST(
    auto_param,
    (StoreReference{
        .variant = StoreReference::Auto{},
        .params =
            {
                {"root", "/foo/bar/baz"},
            },
    }))

static StoreReference localExample_1{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
            .authority = ParsedURL::Authority{},
        },
    .params =
        {
            {"root", "/foo/bar/baz"},
        },
};

static StoreReference localExample_2{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
            .authority = ParsedURL::Authority{},
            .path = {"", "foo", "bar", "baz"},
        },
    .params =
        {
            {"trusted", "true"},
        },
};

#ifdef _WIN32
static StoreReference localExample_windows{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
            .authority = ParsedURL::Authority{},
            .path = {"", "C:", "foo", "bar", "baz"},
        },
    .params =
        {
            {"trusted", "true"},
        },
};
#endif

static StoreReference localExample_3{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
            .authority = ParsedURL::Authority{},
        },
    .params =
        {
            {"root", "/foo bar/baz"},
        },
};

URI_TEST(local_1, localExample_1)

URI_TEST(local_2, localExample_2)

/* Test path with encoded spaces */
URI_TEST(local_3, localExample_3)

/* Test path with spaces that are improperly not encoded */
URI_TEST_READ(local_3_no_percent, localExample_3)

URI_TEST_READ(local_shorthand_1, localExample_1)

#ifndef _WIN32
URI_TEST_READ(local_shorthand_path_unix, localExample_2)
#else
URI_TEST_READ(local_shorthand_path_windows, localExample_windows)
#endif

URI_TEST(
    local_shorthand_3,
    (StoreReference{
        .variant = StoreReference::Local{},
        .params = {},
    }))

static StoreReference unixExample{
    .variant =
        StoreReference::Specified{
            .scheme = "unix",
            .authority = ParsedURL::Authority{},
        },
    .params =
        {
            {"max-connections", "7"},
            {"trusted", "true"},
        },
};

URI_TEST(unix, unixExample)

URI_TEST_READ(unix_shorthand, unixExample)

URI_TEST(
    ssh,
    (StoreReference{
        .variant =
            StoreReference::Specified{
                .scheme = "ssh",
                .authority = ParsedURL::Authority::parse("localhost"),
            },
        .params = {},
    }))

URI_TEST(
    daemon_shorthand,
    (StoreReference{
        .variant = StoreReference::Daemon{},
        .params = {},
    }))

static StoreReference sshLoopbackIPv6{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("[::1]"),
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_1, sshLoopbackIPv6)

static StoreReference sshIPv6AuthorityWithUserinfo{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e]"),
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_2, sshIPv6AuthorityWithUserinfo)

static StoreReference sshIPv6AuthorityWithUserinfoAndParams{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e]"),
        },
    .params =
        {
            {"a", "b"},
            {"c", "d"},
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_3, sshIPv6AuthorityWithUserinfoAndParams)

static const StoreReference sshIPv6AuthorityWithUserinfoAndParamsAndZoneId{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]"),
        },
    .params =
        {
            {"a", "b"},
            {"c", "d"},
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_4, sshIPv6AuthorityWithUserinfoAndParamsAndZoneId)
URI_TEST_READ(ssh_unbracketed_ipv6_5, sshIPv6AuthorityWithUserinfoAndParamsAndZoneId)

static const StoreReference sshIPv6AuthorityWithUserinfoAndParamsAndZoneIdTricky{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%2525]"),
        },
    .params =
        {
            {"a", "b"},
            {"c", "d"},
        },
};

// Non-standard syntax where the IPv6 literal appears without brackets. In
// this case don't considering %25 to be a pct-encoded % and just take it as a
// literal value. 25 is a perfectly legal ZoneId value in theory.
URI_TEST_READ(ssh_unbracketed_ipv6_6, sshIPv6AuthorityWithUserinfoAndParamsAndZoneIdTricky)
URI_TEST_READ(ssh_unbracketed_ipv6_7, sshIPv6AuthorityWithUserinfoAndParamsAndZoneId)

static const StoreReference sshIPv6AuthorityWithParamsAndZoneId{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]"),
        },
    .params =
        {
            {"a", "b"},
            {"c", "d"},
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_8, sshIPv6AuthorityWithParamsAndZoneId)

static const StoreReference sshIPv6AuthorityWithZoneId{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = ParsedURL::Authority::parse("[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]"),
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_9, sshIPv6AuthorityWithZoneId)

TEST_F(StoreReferenceTest, networkStoreWithAuthority)
{
    for (auto scheme : {"ssh", "ssh-ng", "http", "https", "s3"}) {
        for (auto authority : {"127.0.0.1", "localhost", "user@localhost:2222", "[::1]", "user@[::1]:2222"}) {
            auto uri = std::string{scheme} + "://" + authority + "?a=b";
            SCOPED_TRACE(uri);
            StoreReference expected{
                .variant =
                    StoreReference::Specified{.scheme = scheme, .authority = ParsedURL::Authority::parse(authority)},
                .params = {{"a", "b"}},
            };
            EXPECT_EQ(StoreReference::parse(uri), expected);
        }
    }
}

TEST_F(StoreReferenceTest, storeWithoutAuthority)
{
    for (std::string uri : {
             "local:",
             "local:/foo/bar",
             "unix:",
             "unix:/foo/socket",
             "file:/foo/cache",
             "file:./cache",
             "local-overlay:",
             "dummy:",
         }) {
        SCOPED_TRACE(uri);
        auto ref = StoreReference::parse(uri);
        EXPECT_FALSE(std::get<StoreReference::Specified>(ref.variant).authority);
        EXPECT_EQ(ref.render(), uri);
        EXPECT_EQ(StoreReference::parse(uri + "?a=b", {{"c", "d"}}).render(), uri + "?a=b&c=d");
    }
}

TEST_F(StoreReferenceTest, preservesAuthorityAndPath)
{
    struct TestCase
    {
        std::string uri;
        std::optional<ParsedURL::Authority> authority;
        std::vector<std::string> path;
    };

    const TestCase cases[] = {
        {"test-store:host/path", std::nullopt, {"host", "path"}},
        {"test-store:/host/path", std::nullopt, {"", "host", "path"}},
        {"test-store:///host/path", ParsedURL::Authority{}, {"", "host", "path"}},
        {"test-store://host/path", ParsedURL::Authority{.host = "host"}, {"", "path"}},
        {"test-store:foo%2Fbar/baz", std::nullopt, {"foo/bar", "baz"}},
        {"ssh:127.0.0.1", std::nullopt, {"127.0.0.1"}},
    };
    for (auto & test : cases) {
        SCOPED_TRACE(test.uri);
        auto ref = StoreReference::parse(test.uri);
        auto & url = std::get<StoreReference::Specified>(ref.variant);
        EXPECT_EQ(url.authority, test.authority);
        EXPECT_EQ(url.path, test.path);
        EXPECT_EQ(ref.render(), test.uri);
        EXPECT_EQ(StoreReference::parse(ref.render()), ref);
    }
}

TEST_F(StoreReferenceTest, preservesQueryAndFragment)
{
    auto ref = StoreReference::parse("test-store:path%2Fsegment?a=original&b=retained#fragment", {{"a", "override"}});
    auto & url = std::get<StoreReference::Specified>(ref.variant);
    EXPECT_TRUE(url.query.empty());
    EXPECT_EQ(url.fragment, "fragment");
    EXPECT_EQ(ref.params, (StoreReference::Params{{"a", "override"}, {"b", "retained"}}));
    EXPECT_EQ(ref.render(), "test-store:path%2Fsegment?a=override&b=retained#fragment");
    EXPECT_EQ(ref.render(false), "test-store:path%2Fsegment#fragment");
    EXPECT_EQ(StoreReference::parse(ref.render()), ref);
}

TEST_F(StoreReferenceTest, sshStoreLegacyIPv6Authority)
{
    const std::pair<std::string, std::string> cases[] = {
        {"::1", "[::1]"},
        {"user@::1", "user@[::1]"},
        {"user@[fe80::1%eth0]", "user@[fe80::1%25eth0]"},
        {"user@fe80::1%eth0", "user@[fe80::1%25eth0]"},
    };
    for (auto scheme : {"ssh", "ssh-ng"}) {
        for (auto & [authority, expected] : cases) {
            auto prefix = std::string{scheme} + "://";
            auto uri = prefix + authority + "?a=b";
            SCOPED_TRACE(uri);
            EXPECT_EQ(StoreReference::parse(uri).render(), prefix + expected + "?a=b");
        }
    }
}

} // namespace nix
