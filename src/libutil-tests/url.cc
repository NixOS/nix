#include "nix/util/url.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace nix {

/* ----------- tests for url.hh --------------------------------------------------*/

using Authority = ParsedURL::Authority;
using HostType = Authority::HostType;

TEST(parseURL, parsesSimpleHttpUrl)
{
    auto s = "http://www.example.org/file.tar.gz";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = "/file.tar.gz",
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsesSimpleHttpsUrl)
{
    auto s = "https://www.example.org/file.tar.gz";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "https",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = "/file.tar.gz",
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsesSimpleHttpUrlWithQueryAndFragment)
{
    auto s = "https://www.example.org/file.tar.gz?download=fast&when=now#hello";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "https",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = "/file.tar.gz",
        .query = (StringMap) {{"download", "fast"}, {"when", "now"}},
        .fragment = "hello",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parsesSimpleHttpUrlWithComplexFragment)
{
    auto s = "http://www.example.org/file.tar.gz?field=value#?foo=bar%23";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = "/file.tar.gz",
        .query = (StringMap) {{"field", "value"}},
        .fragment = "?foo=bar#",
    };

    ASSERT_EQ(parsed, expected);
}

TEST(parseURL, parsesFilePlusHttpsUrl)
{
    auto s = "file+https://www.example.org/video.mp4";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "file+https",
        .authority = Authority{.hostType = HostType::Name, .host = "www.example.org"},
        .path = "/video.mp4",
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, rejectsAuthorityInUrlsWithFileTransportation)
{
    auto s = "file://www.example.org/video.mp4";
    ASSERT_THROW(parseURL(s), Error);
}

TEST(parseURL, parseIPv4Address)
{
    auto s = "http://127.0.0.1:8080/file.tar.gz?download=fast&when=now#hello";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::IPv4, .host = "127.0.0.1", .port = 8080},
        .path = "/file.tar.gz",
        .query = (StringMap) {{"download", "fast"}, {"when", "now"}},
        .fragment = "hello",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseScopedRFC6874IPv6Address)
{
    auto s = "http://[fe80::818c:da4d:8975:415c\%25enp0s25]:8080";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority = Authority{.hostType = HostType::IPv6, .host = "fe80::818c:da4d:8975:415c\%enp0s25", .port = 8080},
        .path = "",
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
}

TEST(parseURL, parseIPv6Address)
{
    auto s = "http://[2a02:8071:8192:c100:311d:192d:81ac:11ea]:8080";
    auto parsed = parseURL(s);

    ParsedURL expected{
        .scheme = "http",
        .authority =
            Authority{
                .hostType = HostType::IPv6,
                .host = "2a02:8071:8192:c100:311d:192d:81ac:11ea",
                .port = 8080,
            },
        .path = "",
        .query = (StringMap) {},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ(s, parsed.to_string());
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
        .path = "/file.tar.gz",
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
        .path = "/none/of//your/business",
        .query = (StringMap) {},
        .fragment = "",
    };

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
        .path = "/downloads/nixos.iso",
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
        .path = "/file.tar.gz",
        .query = (StringMap) {{"foo", "bar"}},
        .fragment = "",
    };

    ASSERT_EQ(parsed, expected);
    ASSERT_EQ("http://www.example.org/file.tar.gz?foo=bar", parsed.to_string());
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

} // namespace nix
