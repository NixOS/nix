#include "nix/util/url.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <ranges>

namespace nix {

/* ----------- tests for url.hh --------------------------------------------------*/

using Authority = ParsedURL::Authority;
using HostType = Authority::HostType;

struct FixGitURLParam
{
    std::string input;
    std::string expected;
    ParsedURL parsed;
};

std::ostream & operator<<(std::ostream & os, const FixGitURLParam & param)
{
    return os << "Input: \"" << param.input << "\", Expected: \"" << param.expected << "\"";
}

class FixGitURLTestSuite : public ::testing::TestWithParam<FixGitURLParam>
{};

INSTANTIATE_TEST_SUITE_P(
    FixGitURLs,
    FixGitURLTestSuite,
    ::testing::Values(
        // https://github.com/NixOS/nix/issues/5958
        // Already proper URL with git+ssh
        FixGitURLParam{
            .input = "git+ssh://user@domain:1234/path",
            .expected = "ssh://user@domain:1234/path",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .host = "domain",
                            .user = "user",
                            .port = 1234,
                        },
                    .path = {"", "path"},
                },
        },
        // SCP-like URL (rewritten to ssh://)
        FixGitURLParam{
            .input = "git@github.com:owner/repo.git",
            .expected = "ssh://git@github.com/owner/repo.git",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .host = "github.com",
                            .user = "git",
                        },
                    .path = {"", "owner", "repo.git"},
                },
        },
        // SCP-like URL, no user (rewritten to ssh://)
        FixGitURLParam{
            .input = "github.com:owner/repo.git",
            .expected = "ssh://github.com/owner/repo.git",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .host = "github.com",
                        },
                    .path = {"", "owner", "repo.git"},
                },
        },
        // SCP-like URL, no user, absolute path (rewritten to ssh://)
        FixGitURLParam{
            .input = "github.com:/owner/repo.git",
            .expected = "ssh://github.com/owner/repo.git",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .host = "github.com",
                        },
                    .path = {"", "owner", "repo.git"},
                },
        },
        // SCP-like URL (rewritten to ssh://)
        FixGitURLParam{
            .input = "user@server.com:/path/to/repo",
            .expected = "ssh://user@server.com/path/to/repo",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .host = "server.com",
                            .user = "user",
                        },
                    .path = {"", "path", "to", "repo"},
                },
        },
        // Absolute path (becomes file:)
        FixGitURLParam{
            .input = "/home/me/repo",
            .expected = "file:///home/me/repo",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "home", "me", "repo"},
                },
        },
        // Already file: scheme
        FixGitURLParam{
            .input = "file:/var/repos/x",
            .expected = "file:/var/repos/x",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = std::nullopt,
                    .path = {"", "var", "repos", "x"},
                },
        },
        // git+file scheme
        FixGitURLParam{
            .input = "git+file:///var/repos/x",
            .expected = "file:///var/repos/x",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "var", "repos", "x"},
                },
        },
        // absolute path with a space
        FixGitURLParam{
            .input = "/repos/git repo",
            .expected = "file:///repos/git%20repo",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "repos", "git repo"},
                },
        },
        // quoted path
        FixGitURLParam{
            .input = "/repos/\"git repo\"",
            .expected = "file:///repos/%22git%20repo%22",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "repos", "\"git repo\""},
                },
        },
        // IPV6 test case
        FixGitURLParam{
            .input = "user@[2001:db8:1::2]:/home/file",
            .expected = "ssh://user@[2001:db8:1::2]/home/file",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .hostType = HostType::IPv6,
                            .host = "2001:db8:1::2",
                            .user = "user",
                        },
                    .path = {"", "home", "file"},
                },
        },
        // https://github.com/NixOS/nix/issues/14867
        // Verify input doesn't trigger an assert.
        // Intent is git@github, but gets parsed as git scheme with a relative path
        FixGitURLParam{
            .input = "git:github.com:nixos/nixpkgs",
            .expected = "git:github.com:nixos/nixpkgs",
            .parsed =
                ParsedURL{
                    .scheme = "git",
                    .authority = std::nullopt,
                    .path = {"github.com:nixos", "nixpkgs"},
                },
        },
        // https://github.com/NixOS/nix/issues/14867#issuecomment-3699499232
        // Verify input doesn't trigger an assert.
        // The authority should have a "//" prefix, but instead gets parsed as a path component
        FixGitURLParam{
            .input = "git+https:/codeberg.org/forgejo/forgejo",
            .expected = "https:/codeberg.org/forgejo/forgejo",
            .parsed =
                ParsedURL{
                    .scheme = "https",
                    .authority = std::nullopt,
                    .path = {"", "codeberg.org", "forgejo", "forgejo"},
                },
        },
        FixGitURLParam{
            .input = "user%20@[::1]:repo/path",
            .expected = "ssh://user%2520@[::1]/repo/path",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .hostType = ParsedURL::Authority::HostType::IPv6, .host = "::1", .user = "user%20"},
                    .path = {"", "repo", "path"},
                },
        },
        // IPv6 SCP-like. Looks like a port but is actually a path.
        FixGitURLParam{
            .input = "[2a02:8071:8192:c100:311d:192d:81ac:11ea]:12345",
            .expected = "ssh://[2a02:8071:8192:c100:311d:192d:81ac:11ea]/12345",
            .parsed =
                ParsedURL{
                    .scheme = "ssh",
                    .authority =
                        ParsedURL::Authority{
                            .hostType = ParsedURL::Authority::HostType::IPv6,
                            .host = "2a02:8071:8192:c100:311d:192d:81ac:11ea",
                            .user = std::nullopt,
                        },
                    .path = {"", "12345"},
                },
        },
        // Treats percent as a literal and not pct-encoding.
        FixGitURLParam{
            .input = "/a/b/%20",
            .expected = "file:///a/b/%2520",
            .parsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "a", "b", "%20"},
                },
        }));

TEST_P(FixGitURLTestSuite, parsesVariedGitUrls)
{
    auto & p = GetParam();
    const auto actual = fixGitURL(p.input);
    EXPECT_EQ(actual, p.parsed);
    EXPECT_EQ(actual.to_string(), p.expected);
}

// This is an idempotence-like condition: every SCP URL has a corresponding bona fide URL that will parse correctly.
TEST_P(FixGitURLTestSuite, parsedNormalized)
{
    auto & p = GetParam();
    const auto actual = fixGitURL(p.expected);
    EXPECT_EQ(actual, p.parsed);
    EXPECT_EQ(actual.to_string(), p.expected);
}

TEST(FixGitURLTestSuite, rejectFileURLWithAuthority)
{
    /* From the underlying `parseURL` validations. */
    EXPECT_THAT(
        []() { fixGitURL("file://var/repos/x"); },
        ::testing::ThrowsMessage<BadURL>(
            testing::HasSubstrIgnoreANSIMatcher("file:// URL 'file://var/repos/x' has unexpected authority 'var'")));
}

TEST(FixGitURLTestSuite, rejectRelativePath)
{
    /* From the underlying `parseURL` validations. */
    EXPECT_THAT(
        []() { fixGitURL("relative/repo"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("is not an absolute path")));
}

TEST(FixGitURLTestSuite, rejectEmptyPathGitScp)
{
    /* Reject SCP-style URLs with no path component. */
    EXPECT_THAT(
        []() { fixGitURL("host:"); },
        ::testing::ThrowsMessage<BadURL>(
            testing::HasSubstrIgnoreANSIMatcher("SCP-style Git URL 'host:' has an empty path")));
}

TEST(FixGitURLTestSuite, rejectMalformedBracketedURLs)
{
    /* Reject URLs with brackets that don't form valid SCP-style IPv6 syntax. */
    EXPECT_THAT(
        []() { fixGitURL("user[2001:db8:1::2]:/home/@file"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("is not a valid URL")));
    EXPECT_THAT(
        []() { fixGitURL("user:[2001:db8:1::2]:/home/@file"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("is not a valid URL")));
    EXPECT_THAT(
        []() { fixGitURL("user:@[2001:db8:1::2]:/home/file"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("is not a valid URL")));
}

struct ParseURLSuccessCase
{
    std::string_view input;
    ParsedURL expected;
};

class ParseURLSuccess : public ::testing::TestWithParam<ParseURLSuccessCase>
{};

INSTANTIATE_TEST_SUITE_P(
    ParseURLSuccessCases,
    ParseURLSuccess,
    ::testing::Values(
        ParseURLSuccessCase{
            .input = "http://www.example.org/file.tar.gz",
            .expected =
                ParsedURL{
                    .scheme = "http",
                    .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
                    .path = {"", "file.tar.gz"},
                    .query = (StringMap) {},
                    .fragment = "",
                },
        },
        ParseURLSuccessCase{
            .input = "https://www.example.org/file.tar.gz",
            .expected =
                ParsedURL{
                    .scheme = "https",
                    .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
                    .path = {"", "file.tar.gz"},
                    .query = (StringMap) {},
                    .fragment = "",
                },
        },
        ParseURLSuccessCase{
            .input = "https://www.example.org/file.tar.gz?download=fast&when=now#hello",
            .expected =
                ParsedURL{
                    .scheme = "https",
                    .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
                    .path = {"", "file.tar.gz"},
                    .query = (StringMap) {{"download", "fast"}, {"when", "now"}},
                    .fragment = "hello",
                },
        },
        ParseURLSuccessCase{
            .input = "file+https://www.example.org/video.mp4",
            .expected =
                ParsedURL{
                    .scheme = "file+https",
                    .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
                    .path = {"", "video.mp4"},
                    .query = (StringMap) {},
                    .fragment = "",
                },
        },
        ParseURLSuccessCase{
            .input = "http://127.0.0.1:8080/file.tar.gz?download=fast&when=now#hello",
            .expected =
                ParsedURL{
                    .scheme = "http",
                    .authority = Authority{.hostType = HostType::IPv4, .host = "127.0.0.1", .port = 8080},
                    .path = {"", "file.tar.gz"},
                    .query = (StringMap) {{"download", "fast"}, {"when", "now"}},
                    .fragment = "hello",
                },
        },
        ParseURLSuccessCase{
            .input = "http://[fe80::818c:da4d:8975:415c\%25enp0s25]:8080",
            .expected =
                ParsedURL{
                    .scheme = "http",
                    .authority =
                        Authority{
                            .hostType = HostType::IPv6, .host = "fe80::818c:da4d:8975:415c\%enp0s25", .port = 8080},
                    .path = {""},
                    .query = (StringMap) {},
                    .fragment = "",
                },

        },
        ParseURLSuccessCase{
            .input = "http://[2a02:8071:8192:c100:311d:192d:81ac:11ea]:8080",
            .expected =
                ParsedURL{
                    .scheme = "http",
                    .authority =
                        Authority{
                            .hostType = HostType::IPv6,
                            .host = "2a02:8071:8192:c100:311d:192d:81ac:11ea",
                            .port = 8080,
                        },
                    .path = {""},
                    .query = (StringMap) {},
                    .fragment = "",
                },
        }));

TEST_P(ParseURLSuccess, parsesAsExpected)
{
    auto & p = GetParam();
    const auto parsed = parseURL(p.input);
    EXPECT_EQ(parsed, p.expected);
}

TEST_P(ParseURLSuccess, toStringRoundTrips)
{
    auto & p = GetParam();
    const auto parsed = parseURL(p.input);
    EXPECT_EQ(p.input, parsed.to_string());
}

TEST_P(ParseURLSuccess, makeSureFixGitURLDoesNotModify)
{
    auto & p = GetParam();
    const auto parsed = fixGitURL(std::string{p.input});
    EXPECT_EQ(p.input, parsed.to_string());
}

TEST(parseURL, parsesSimpleHttpUrlWithComplexFragment)
{
    auto s = "http://www.example.org/file.tar.gz?field=value#?foo=bar%23";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "file.tar.gz"},
        .query = (StringMap) {{"field", "value"}},
        .fragment = "?foo=bar#",
    };

    ASSERT_EQ(parsed, expected);
}

TEST(parseURL, rejectsAuthorityInUrlsWithFileTransportation)
{
    EXPECT_THAT(
        []() { parseURL("file://www.example.org/video.mp4"); },
        ::testing::ThrowsMessage<BadURL>(
            testing::HasSubstrIgnoreANSIMatcher("has unexpected authority 'www.example.org'")));
}

TEST(parseURL, parseEmptyQueryParams)
{
    auto s = "http://127.0.0.1:8080/file.tar.gz?&&&&&";
    auto parsed = parseURL(s);
    ASSERT_EQ(parsed.query, (StringMap) {});
}

TEST(parseURL, parseUserPassword)
{
    auto s = "http://user:pass@www.example.org:8080/file.tar.gz";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority =
            Authority{
                .hostType = HostType::Name,
                .host = "www.example.org",
                .user = "user",
                .password = "pass",
                .port = 8080,
            },
        .path = {"", "file.tar.gz"},
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseFileURLWithQueryAndFragment)
{
    auto s = "file:///none/of//your/business";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "file",
        .authority = Authority{},
        .path = {"", "none", "of", "", "your", "business"},
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed.renderPath(), "/none/of//your/business");
    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseFileURL)
{
    auto s = "file:/none/of/your/business/";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "file",
        .authority = std::nullopt,
        .path = {"", "none", "of", "your", "business", ""},
    };

    ASSERT_EQ(parsed.renderPath(), "/none/of/your/business/");
    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseFileURLWithAuthority)
{
    auto s = "file://///of/your/business//";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "file",
        .authority = Authority{.host = ""},
        .path = {"", "", "", "of", "your", "business", "", ""},
    };

    ASSERT_EQ(parsed.path, expected.path);
    ASSERT_EQ(parsed.renderPath(), "///of/your/business//");
    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseFileURLNoLeadingSlash)
{
    auto s = "file:none/of/your/business/";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "file",
        .authority = std::nullopt,
        .path = {"none", "of", "your", "business", ""},
    };

    ASSERT_EQ(parsed.renderPath(), "none/of/your/business/");
    ASSERT_EQ(parsed, expected);
    ASSERT_EQ("file:none/of/your/business/", parsed.to_string());
}

TEST(parseURL, parseHttpTrailingSlash)
{
    auto s = "http://example.com/";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.host = "example.com"},
        .path = {"", ""},
    };

    ASSERT_EQ(parsed.renderPath(), "/");
    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsedUrlsIsEqualToItself)
{
    auto s = "http://www.example.org/file.tar.gz";
    auto url = parseURL(s);

    ASSERT_TRUE(url == url);
}

TEST(parseURL, parsedUrlsWithUnescapedChars)
{
    /* Test for back-compat. Behavior is rather questionable, but
     * is ingrained pretty deep into how URL parsing is shared between
     * flakes and libstore.
     * 1. Unescaped spaces, quotes and shevron (^) in fragment.
     * 2. Unescaped spaces and quotes in query.
     */
    auto s = "http://www.example.org/file.tar.gz?query \"= 123\"#shevron^quote\"space ";

    /* Without leniency for back compat, this should throw. */
    EXPECT_THROW(parseURL(s), Error);

    /* With leniency for back compat, this should parse. */
    auto url = parseURL(s, /*lenient=*/true);

    EXPECT_EQ(url.fragment, "shevron^quote\"space ");

    auto query = StringMap{
        {"query \"", " 123\""},
    };

    EXPECT_EQ(url.query, query);
}

TEST(parseURL, parseFTPUrl)
{
    auto s = "ftp://ftp.nixos.org/downloads/nixos.iso";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "ftp",
        .authority = Authority{.hostType = HostType::Name, .host = "ftp.nixos.org"},
        .path = {"", "downloads", "nixos.iso"},
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsesAnythingInUriFormat)
{
    auto s = "whatever://github.com/NixOS/nixpkgs.git";
    auto parsed = parseURL(s);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsesAnythingInUriFormatWithoutDoubleSlash)
{
    auto s = "whatever:github.com/NixOS/nixpkgs.git";
    auto parsed = parseURL(s);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, emptyStringIsInvalidURL)
{
    ASSERT_THROW(parseURL(""), Error);
}

TEST(parseURL, parsesHttpUrlWithEmptyPort)
{
    auto s = "http://www.example.org:/file.tar.gz?foo=bar";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "file.tar.gz"},
        .query = (StringMap) {{"foo", "bar"}},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ("http://www.example.org/file.tar.gz?foo=bar", parsed.to_string());
}

/* ----------------------------------------------------------------------------
 * parseURLRelative
 * --------------------------------------------------------------------------*/

TEST(parseURLRelative, resolvesRelativePath)
{
    ParsedURL base = parseURL("http://example.org/dir/page.html");
    auto parsed = parseURLRelative("subdir/file.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "example.org"},
        .path = {"", "dir", "subdir", "file.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, baseUrlIpv6AddressWithoutZoneId)
{
    ParsedURL base = parseURL("http://[fe80::818c:da4d:8975:415c]/dir/page.html");
    auto parsed = parseURLRelative("subdir/file.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::IPv6, .host = "fe80::818c:da4d:8975:415c"},
        .path = {"", "dir", "subdir", "file.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, resolvesRelativePathIpv6AddressWithZoneId)
{
    ParsedURL base = parseURL("http://[fe80::818c:da4d:8975:415c\%25enp0s25]:8080/dir/page.html");
    auto parsed = parseURLRelative("subdir/file2.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::IPv6, .host = "fe80::818c:da4d:8975:415c\%enp0s25", .port = 8080},
        .path = {"", "dir", "subdir", "file2.txt"},
        .query = {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, resolvesRelativePathWithDot)
{
    ParsedURL base = parseURL("http://example.org/dir/page.html");
    auto parsed = parseURLRelative("./subdir/file.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "example.org"},
        .path = {"", "dir", "subdir", "file.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, resolvesParentDirectory)
{
    ParsedURL base = parseURL("http://example.org:234/dir/page.html");
    auto parsed = parseURLRelative("../up.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "example.org", .port = 234},
        .path = {"", "up.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, resolvesParentDirectoryNotTrickedByEscapedSlash)
{
    ParsedURL base = parseURL("http://example.org:234/dir\%2Ffirst-trick/another-dir\%2Fsecond-trick/page.html");
    auto parsed = parseURLRelative("../up.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "example.org", .port = 234},
        .path = {"", "dir/first-trick", "up.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, replacesPathWithAbsoluteRelative)
{
    ParsedURL base = parseURL("http://example.org/dir/page.html");
    auto parsed = parseURLRelative("/rooted.txt", base);
    ParsedURL expected{
        .scheme = "http",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "example.org"},
        .path = {"", "rooted.txt"},
        .query = {},
        .fragment = "",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, keepsQueryAndFragmentFromRelative)
{
    // But discard query params on base URL
    ParsedURL base = parseURL("https://www.example.org/path/index.html?z=3");
    auto parsed = parseURLRelative("other.html?x=1&y=2#frag", base);
    ParsedURL expected{
        .scheme = "https",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "path", "other.html"},
        .query = {{"x", "1"}, {"y", "2"}},
        .fragment = "frag",
    };
    ASSERT_EQ(parsed, expected);
}

TEST(parseURLRelative, absOverride)
{
    ParsedURL base = parseURL("http://example.org/path/page.html");
    std::string_view abs = "https://127.0.0.1.org/secure";
    auto parsed = parseURLRelative(abs, base);
    auto parsedAbs = parseURL(abs);
    ASSERT_EQ(parsed, parsedAbs);
}

TEST(parseURLRelative, absOverrideWithZoneId)
{
    ParsedURL base = parseURL("http://example.org/path/page.html");
    std::string_view abs = "https://[fe80::818c:da4d:8975:415c\%25enp0s25]/secure?foo=bar";
    auto parsed = parseURLRelative(abs, base);
    auto parsedAbs = parseURL(abs);
    ASSERT_EQ(parsed, parsedAbs);
}

TEST(parseURLRelative, bothWithoutAuthority)
{
    ParsedURL base = parseURL("mailto:mail-base@bar.baz?bcc=alice@asdf.com");
    std::string_view over = "mailto:mail-override@foo.bar?subject=url-testing";
    auto parsed = parseURLRelative(over, base);
    auto parsedOverride = parseURL(over);
    ASSERT_EQ(parsed, parsedOverride);
}

TEST(parseURLRelative, emptyRelative)
{
    ParsedURL base = parseURL("https://www.example.org/path/index.html?a\%20b=5\%206&x\%20y=34#frag");
    auto parsed = parseURLRelative("", base);
    ParsedURL expected{
        .scheme = "https",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "path", "index.html"},
        .query = {{"a b", "5 6"}, {"x y", "34"}},
        .fragment = "",
    };
    EXPECT_EQ(base.fragment, "frag");
    EXPECT_EQ(parsed, expected);
}

TEST(parseURLRelative, fragmentRelative)
{
    ParsedURL base = parseURL("https://www.example.org/path/index.html?a\%20b=5\%206&x\%20y=34#frag");
    auto parsed = parseURLRelative("#frag2", base);
    ParsedURL expected{
        .scheme = "https",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "path", "index.html"},
        .query = {{"a b", "5 6"}, {"x y", "34"}},
        .fragment = "frag2",
    };
    EXPECT_EQ(parsed, expected);
}

TEST(parseURLRelative, queryRelative)
{
    ParsedURL base = parseURL("https://www.example.org/path/index.html?a\%20b=5\%206&x\%20y=34#frag");
    auto parsed = parseURLRelative("?asdf\%20qwer=1\%202\%203", base);
    ParsedURL expected{
        .scheme = "https",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "path", "index.html"},
        .query = {{"asdf qwer", "1 2 3"}},
        .fragment = "",
    };
    EXPECT_EQ(parsed, expected);
}

TEST(parseURLRelative, queryFragmentRelative)
{
    ParsedURL base = parseURL("https://www.example.org/path/index.html?a\%20b=5\%206&x\%20y=34#frag");
    auto parsed = parseURLRelative("?asdf\%20qwer=1\%202\%203#frag2", base);
    ParsedURL expected{
        .scheme = "https",
        .authority = ParsedURL::Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = {"", "path", "index.html"},
        .query = {{"asdf qwer", "1 2 3"}},
        .fragment = "frag2",
    };
    EXPECT_EQ(parsed, expected);
}

/* ----------------------------------------------------------------------------
 * decodeQuery
 * --------------------------------------------------------------------------*/

TEST(decodeQuery, emptyStringYieldsEmptyMap)
{
    auto d = decodeQuery("");
    ASSERT_EQ(d, (StringMap) {});
}

TEST(decodeQuery, simpleDecode)
{
    auto d = decodeQuery("yi=one&er=two");
    ASSERT_EQ(d, ((StringMap) {{"yi", "one"}, {"er", "two"}}));
}

TEST(decodeQuery, decodeUrlEncodedArgs)
{
    auto d = decodeQuery("arg=%3D%3D%40%3D%3D");
    ASSERT_EQ(d, ((StringMap) {{"arg", "==@=="}}));
}

TEST(decodeQuery, decodeArgWithEmptyValue)
{
    auto d = decodeQuery("arg=");
    ASSERT_EQ(d, ((StringMap) {{"arg", ""}}));
}

/* ----------------------------------------------------------------------------
 * percentDecode
 * --------------------------------------------------------------------------*/

TEST(percentDecode, decodesUrlEncodedString)
{
    std::string s = "==@==";
    std::string d = percentDecode("%3D%3D%40%3D%3D");
    ASSERT_EQ(d, s);
}

TEST(percentDecode, multipleDecodesAreIdempotent)
{
    std::string once = percentDecode("%3D%3D%40%3D%3D");
    std::string twice = percentDecode(once);

    ASSERT_EQ(once, twice);
}

TEST(percentDecode, trailingPercent)
{
    std::string s = "==@==%";
    std::string d = percentDecode("%3D%3D%40%3D%3D%25");

    ASSERT_EQ(d, s);
}

TEST(percentDecode, incompleteEncoding)
{
    ASSERT_THAT(
        []() { percentDecode("%1"); },
        ::testing::ThrowsMessage<BadURL>(
            testing::HasSubstrIgnoreANSIMatcher("error: invalid URI parameter '%1': incomplete pct-encoding")));
}

/* ----------------------------------------------------------------------------
 * percentEncode
 * --------------------------------------------------------------------------*/

TEST(percentEncode, encodesUrlEncodedString)
{
    std::string s = percentEncode("==@==");
    std::string d = "%3D%3D%40%3D%3D";
    ASSERT_EQ(d, s);
}

TEST(percentEncode, keepArgument)
{
    std::string a = percentEncode("abd / def");
    std::string b = percentEncode("abd / def", "/");
    ASSERT_EQ(a, "abd%20%2F%20def");
    ASSERT_EQ(b, "abd%20/%20def");
}

TEST(percentEncode, inverseOfDecode)
{
    std::string original = "%3D%3D%40%3D%3D";
    std::string once = percentEncode(original);
    std::string back = percentDecode(once);

    ASSERT_EQ(back, original);
}

TEST(percentEncode, trailingPercent)
{
    std::string s = percentEncode("==@==%");
    std::string d = "%3D%3D%40%3D%3D%25";

    ASSERT_EQ(d, s);
}

TEST(percentEncode, yen)
{
    // https://en.wikipedia.org/wiki/Percent-encoding#Character_data
    std::string s = reinterpret_cast<const char *>(u8"円");
    std::string e = "%E5%86%86";

    ASSERT_EQ(percentEncode(s), e);
    ASSERT_EQ(percentDecode(e), s);
}

TEST(parseURL, gitlabNamespacedProjectUrls)
{
    // Test GitLab URL patterns with namespaced projects
    // These should preserve %2F encoding in the path
    auto s = "https://gitlab.example.com/api/v4/projects/group%2Fsubgroup%2Fproject/repository/archive.tar.gz";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "https",
        .authority = Authority{.hostType = HostType::Name, .host = "gitlab.example.com"},
        .path = {"", "api", "v4", "projects", "group/subgroup/project", "repository", "archive.tar.gz"},
        .query = {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

/* ----------------------------------------------------------------------------
 * pathSegments
 * --------------------------------------------------------------------------*/

struct ParsedURLPathSegmentsTestCase
{
    std::string url;
    std::vector<std::string> segments;
    std::string path;
    bool skipEmpty;
    std::string description;
};

class ParsedURLPathSegmentsTest : public ::testing::TestWithParam<ParsedURLPathSegmentsTestCase>
{};

TEST_P(ParsedURLPathSegmentsTest, segmentsAreCorrect)
{
    const auto & testCase = GetParam();
    auto segments = parseURL(testCase.url).pathSegments(/*skipEmpty=*/testCase.skipEmpty)
                    | std::ranges::to<decltype(testCase.segments)>();
    EXPECT_EQ(segments, testCase.segments);
    EXPECT_EQ(encodeUrlPath(segments), testCase.path);
}

TEST_P(ParsedURLPathSegmentsTest, to_string)
{
    const auto & testCase = GetParam();
    EXPECT_EQ(testCase.url, parseURL(testCase.url).to_string());
}

INSTANTIATE_TEST_SUITE_P(
    ParsedURL,
    ParsedURLPathSegmentsTest,
    ::testing::Values(
        ParsedURLPathSegmentsTestCase{
            .url = "scheme:",
            .segments = {""},
            .path = "",
            .skipEmpty = false,
            .description = "no_authority_empty_path",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme://",
            .segments = {""},
            .path = "",
            .skipEmpty = false,
            .description = "empty_authority_empty_path",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "path:/",
            .segments = {"", ""},
            .path = "/",
            .skipEmpty = false,
            .description = "empty_authority_root_path",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme:///",
            .segments = {"", ""},
            .path = "/",
            .skipEmpty = false,
            .description = "empty_authority_empty_path_trailing",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme://example.com/",
            .segments = {"", ""},
            .path = "/",
            .skipEmpty = false,
            .description = "non_empty_authority_empty_path",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme://example.com//",
            .segments = {"", "", ""},
            .path = "//",
            .skipEmpty = false,
            .description = "non_empty_authority_non_empty_path",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme://example.com///path///with//strange/empty///segments////",
            .segments = {"path", "with", "strange", "empty", "segments"},
            .path = "path/with/strange/empty/segments",
            .skipEmpty = true,
            .description = "skip_all_empty_segments_with_authority",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme://example.com///lots///empty///",
            .segments = {"", "", "", "lots", "", "", "empty", "", "", ""},
            .path = "///lots///empty///",
            .skipEmpty = false,
            .description = "empty_segments_with_authority",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme:/path///with//strange/empty///segments////",
            .segments = {"path", "with", "strange", "empty", "segments"},
            .path = "path/with/strange/empty/segments",
            .skipEmpty = true,
            .description = "skip_all_empty_segments_no_authority_starts_with_slash",
        },
        ParsedURLPathSegmentsTestCase{
            .url = "scheme:path///with//strange/empty///segments////",
            .segments = {"path", "with", "strange", "empty", "segments"},
            .path = "path/with/strange/empty/segments",
            .skipEmpty = true,
            .description = "skip_all_empty_segments_no_authority_doesnt_start_with_slash",
        }),
    [](const auto & info) { return info.param.description; });

TEST(nix, isValidSchemeName)

{
    ASSERT_TRUE(isValidSchemeName("http"));
    ASSERT_TRUE(isValidSchemeName("https"));
    ASSERT_TRUE(isValidSchemeName("file"));
    ASSERT_TRUE(isValidSchemeName("file+https"));
    ASSERT_TRUE(isValidSchemeName("fi.le"));
    ASSERT_TRUE(isValidSchemeName("file-ssh"));
    ASSERT_TRUE(isValidSchemeName("file+"));
    ASSERT_TRUE(isValidSchemeName("file."));
    ASSERT_TRUE(isValidSchemeName("file1"));
    ASSERT_FALSE(isValidSchemeName("file:"));
    ASSERT_FALSE(isValidSchemeName("file/"));
    ASSERT_FALSE(isValidSchemeName("+file"));
    ASSERT_FALSE(isValidSchemeName(".file"));
    ASSERT_FALSE(isValidSchemeName("-file"));
    ASSERT_FALSE(isValidSchemeName("1file"));
    // regex ok?
    ASSERT_FALSE(isValidSchemeName("\nhttp"));
    ASSERT_FALSE(isValidSchemeName("\nhttp\n"));
    ASSERT_FALSE(isValidSchemeName("http\n"));
    ASSERT_FALSE(isValidSchemeName("http "));
}

/* ----------------------------------------------------------------------------
 * pathToUrlPath / urlPathToPath
 * --------------------------------------------------------------------------*/

struct UrlPathTestCase
{
    std::string_view urlString;
    ParsedURL urlParsed;
    std::filesystem::path path;
    std::string description;
};

class UrlPathTest : public ::testing::TestWithParam<UrlPathTestCase>
{};

TEST_P(UrlPathTest, pathToUrlPath)
{
    const auto & testCase = GetParam();
    auto urlPath = pathToUrlPath(testCase.path);
    EXPECT_EQ(urlPath, testCase.urlParsed.path);
}

TEST_P(UrlPathTest, urlPathToPath)
{
    const auto & testCase = GetParam();
    auto path = urlPathToPath(testCase.urlParsed.path);
    EXPECT_EQ(path, testCase.path);
}

TEST_P(UrlPathTest, urlToString)
{
    const auto & testCase = GetParam();
    EXPECT_EQ(testCase.urlParsed.to_string(), testCase.urlString);
}

TEST_P(UrlPathTest, stringToUrl)
{
    const auto & testCase = GetParam();
    auto parsed = parseURL(std::string{testCase.urlString});
    EXPECT_EQ(parsed, testCase.urlParsed);
}

#ifndef _WIN32

INSTANTIATE_TEST_SUITE_P(
    Unix,
    UrlPathTest,
    ::testing::Values(
        UrlPathTestCase{
            .urlString = "file:///foo/bar/baz",
            .urlParsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "foo", "bar", "baz"},
                },
            .path = "/foo/bar/baz",
            .description = "absolute_path",
        },
        UrlPathTestCase{
            .urlString = "file:///",
            .urlParsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", ""},
                },
            .path = "/",
            .description = "root_path",
        }),
    [](const auto & info) { return info.param.description; });

#else // _WIN32

INSTANTIATE_TEST_SUITE_P(
    Windows,
    UrlPathTest,
    ::testing::Values(
        UrlPathTestCase{
            .urlString = "file:///C:/foo/bar/baz",
            .urlParsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "C:", "foo", "bar", "baz"},
                },
            .path = L"C:\\foo\\bar\\baz",
            .description = "absolute_path",
        },
        UrlPathTestCase{
            .urlString = "file:///C:/",
            .urlParsed =
                ParsedURL{
                    .scheme = "file",
                    .authority = ParsedURL::Authority{},
                    .path = {"", "C:", ""},
                },
            .path = L"C:\\",
            .description = "drive_root",
        }),
    [](const auto & info) { return info.param.description; });

#endif // _WIN32

} // namespace nix
