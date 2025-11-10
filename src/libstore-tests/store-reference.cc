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

    std::filesystem::path goldenMaster(PathView testStem) const override
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
            .authority = "/foo/bar/baz",
        },
    .params =
        {
            {"trusted", "true"},
        },
};

static StoreReference localExample_3{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
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

URI_TEST_READ(local_shorthand_2, localExample_2)

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
                .authority = "localhost",
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
            .authority = "[::1]",
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_1, sshLoopbackIPv6)

static StoreReference sshIPv6AuthorityWithUserinfo{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = "userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e]",
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_2, sshIPv6AuthorityWithUserinfo)

static StoreReference sshIPv6AuthorityWithUserinfoAndParams{
    .variant =
        StoreReference::Specified{
            .scheme = "ssh",
            .authority = "userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e]",
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
            .authority = "userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]",
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
            .authority = "userinfo@[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%2525]",
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
            .authority = "[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]",
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
            .authority = "[fea5:23e1:3916:fc24:cb52:2837:2ecb:ea8e%25eth0]",
        },
};

URI_TEST_READ(ssh_unbracketed_ipv6_9, sshIPv6AuthorityWithZoneId)

} // namespace nix
