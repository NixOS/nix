#include <gtest/gtest.h>

#include "nix/util/logical-path.hh"
#include "nix/util/url.hh"
#include "nix/util/wtf8.hh"

namespace nix {

/* --- Root recognition ------------------------------------------------- */

TEST(logicalPath, posixAbsolute)
{
    auto p = LogicalPath::parseNative("/usr/lib");
    EXPECT_TRUE(std::holds_alternative<LogicalPath::Posix>(p.root()));
    EXPECT_EQ(p.components(), (std::vector<std::string>{"usr", "lib"}));
    EXPECT_EQ(p.toUri(), "file:///usr/lib");
}

TEST(logicalPath, relativeIsAnchoredToPosixRoot)
{
    /* A relative path has no root name, so it lands on `Posix` -- the
       same collapse `CanonPath` performs, and a known limitation. */
    auto p = LogicalPath::parseNative("usr/lib");
    EXPECT_TRUE(std::holds_alternative<LogicalPath::Posix>(p.root()));
    EXPECT_EQ(p.components(), (std::vector<std::string>{"usr", "lib"}));
}

TEST(logicalPath, driveLetterUpperAndLowerAgree)
{
    auto upper = LogicalPath::parseNative("C:\\foo");
    auto lower = LogicalPath::parseNative("c:\\foo");

    /* RFC 8089 appendix E.2 states drive-letter comparison is
       case-insensitive, so these must be one path, not two. */
    EXPECT_EQ(upper, lower);
    EXPECT_EQ(upper.toUri(), "file:///C:/foo");
    EXPECT_EQ(lower.toUri(), "file:///C:/foo");
}

TEST(logicalPath, theConstructorFoldsCaseToo)
{
    /* `parseNative` folds in `parseNativeRoot`, so the two tests above
       pass without the constructor doing anything. The constructor is
       public, so it has to fold as well, and only building a value
       directly reaches that code. */
    LogicalPath lower{LogicalPath::Drive{'c'}, {"foo"}};
    LogicalPath upper{LogicalPath::Drive{'C'}, {"foo"}};
    EXPECT_EQ(lower, upper);
    EXPECT_EQ(lower.toUri(), "file:///C:/foo");

    LogicalPath mixedHost{LogicalPath::Unc{.host = "HoSt", .share = "s"}, {"f"}};
    EXPECT_EQ(std::get<LogicalPath::Unc>(mixedHost.root()).host, "host");
}

TEST(logicalPath, unc)
{
    auto p = LogicalPath::parseNative("\\\\host.example.com\\Share\\path\\to\\file.txt");
    auto * u = std::get_if<LogicalPath::Unc>(&p.root());
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->host, "host.example.com");
    EXPECT_EQ(u->share, "Share");
    EXPECT_EQ(p.components(), (std::vector<std::string>{"path", "to", "file.txt"}));

    /* The mapping given by RFC 8089 appendix E.3.1. */
    EXPECT_EQ(p.toUri(), "file://host.example.com/Share/path/to/file.txt");
}

TEST(logicalPath, uncHostIsCaseFolded)
{
    EXPECT_EQ(LogicalPath::parseNative("\\\\HOST\\s\\f"), LogicalPath::parseNative("\\\\host\\s\\f"));
}

TEST(logicalPath, uncShareIsNotCaseFolded)
{
    /* RFC 3986 leaves the path case-sensitive even though Windows does
       not. Folding it here would make two distinct URIs compare equal. */
    EXPECT_NE(LogicalPath::parseNative("\\\\h\\SHARE\\f"), LogicalPath::parseNative("\\\\h\\share\\f"));
}

TEST(logicalPath, extendedLengthDriveNormalisesToDrive)
{
    /* `\\?\C:` names the same volume as `C:`. */
    EXPECT_EQ(LogicalPath::parseNative("\\\\?\\C:\\foo"), LogicalPath::parseNative("C:\\foo"));
}

TEST(logicalPath, extendedLengthUncNormalisesToUnc)
{
    EXPECT_EQ(LogicalPath::parseNative("\\\\?\\UNC\\h\\s\\foo"), LogicalPath::parseNative("\\\\h\\s\\foo"));
}

TEST(logicalPath, deviceGetsItsOwnScheme)
{
    auto p = LogicalPath::parseNative("\\\\.\\PhysicalDrive0\\foo");
    auto * d = std::get_if<LogicalPath::Device>(&p.root());
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->name, "PhysicalDrive0");

    /* RFC 8089 states it does not define a mechanism for these, so this
       must not masquerade as a conformant `file` URI. */
    EXPECT_EQ(p.toUri(), "nix-win32-device:///PhysicalDrive0/foo");
}

/* --- Canonicalisation ------------------------------------------------- */

TEST(logicalPath, dotAndDotDotAreResolved)
{
    EXPECT_EQ(LogicalPath::parseNative("C:\\a\\.\\b\\..\\c"), LogicalPath::parseNative("C:\\a\\c"));
}

TEST(logicalPath, dotDotCannotEscapeTheRoot)
{
    /* Escaping a share would silently reach a different volume. */
    EXPECT_EQ(LogicalPath::parseNative("\\\\h\\s\\..\\..\\x"), LogicalPath::parseNative("\\\\h\\s\\x"));
}

/* --- URI round trips, one per input class ----------------------------- */

struct RoundTrip
{
    const char * name;
    const char * native;
};

class LogicalPathRoundTrip : public ::testing::TestWithParam<RoundTrip>
{};

TEST_P(LogicalPathRoundTrip, nativeToUriToNative)
{
    auto original = LogicalPath::parseNative(GetParam().native);
    auto uri = original.toUri();
    auto back = LogicalPath::parseUri(uri);
    EXPECT_EQ(back, original) << "via URI " << uri;
    EXPECT_EQ(back.toUri(), uri);
}

INSTANTIATE_TEST_SUITE_P(
    inputClasses,
    LogicalPathRoundTrip,
    ::testing::Values(
        RoundTrip{"posixAbsolute", "/usr/lib/x"},
        RoundTrip{"posixRootOnly", "/"},
        RoundTrip{"relative", "a/b"},
        RoundTrip{"driveUpper", "C:\\foo\\bar"},
        RoundTrip{"driveLower", "c:\\foo\\bar"},
        RoundTrip{"driveRootOnly", "C:\\"},
        RoundTrip{"unc", "\\\\host\\share\\a\\b"},
        RoundTrip{"uncShareOnly", "\\\\host\\share"},
        RoundTrip{"extendedDrive", "\\\\?\\C:\\foo"},
        RoundTrip{"extendedUnc", "\\\\?\\UNC\\host\\share\\foo"},
        RoundTrip{"device", "\\\\.\\PhysicalDrive0\\foo"}),
    [](const auto & info) { return info.param.name; });

TEST(logicalPath, reservedCharactersSurviveTheRoundTrip)
{
    /* Percent-encoding a name that already contains `%` is where a naive
       implementation loses data: encode must produce `%25`, and decode
       must not then re-decode it. `#` and `?` would otherwise be read as
       the start of a fragment or query. */
    std::vector<std::string> nasty{"100%", "a%2Fb", "has space", "hash#tag", "query?mark", "%", "%%", "%41"};

    for (auto & name : nasty) {
        LogicalPath p{LogicalPath::Drive{'C'}, {name}};
        auto uri = p.toUri();
        auto back = LogicalPath::parseUri(uri);
        ASSERT_EQ(back.components().size(), 1u) << "name " << name << " via " << uri;
        EXPECT_EQ(back.components()[0], name) << "via URI " << uri;
    }
}

TEST(logicalPath, literalPercentIsEncodedNotPassedThrough)
{
    LogicalPath p{LogicalPath::Posix{}, {"100%"}};
    EXPECT_EQ(p.toUri(), "file:///100%25");
}

TEST(logicalPath, aSlashInAComponentIsRejected)
{
    /* No real file name contains a separator, and allowing one would
       make `toUri` and `toNative` disagree about how many components
       there are. */
    EXPECT_THROW(LogicalPath(LogicalPath::Posix{}, {"a/b"}), BadLogicalPath);
}

TEST(logicalPath, unpairedSurrogateSurvivesTheRoundTrip)
{
    /* An unpaired surrogate is a legal Windows file name and has no
       UTF-8 encoding, so this is the case RFC 8089 section 4 explicitly
       leaves out of scope. Percent-encoded WTF-8 carries it. */
    std::u16string utf16;
    utf16 += u'a';
    utf16 += char16_t(0xD800);
    utf16 += u'b';

    auto name = wtf8FromUtf16(utf16);
    ASSERT_EQ(name.size(), 5u); /* 'a' + 3 bytes + 'b' */

    LogicalPath p{LogicalPath::Drive{'C'}, {name}};
    auto uri = p.toUri();

    /* The surrogate's WTF-8 bytes are ED A0 80. */
    EXPECT_NE(uri.find("%ED%A0%80"), std::string::npos) << "uri " << uri;

    auto back = LogicalPath::parseUri(uri);
    ASSERT_EQ(back.components().size(), 1u);
    EXPECT_EQ(back.components()[0], name);

    /* And it is still the same UTF-16 sequence after the trip. */
    EXPECT_TRUE(utf16FromWtf8(back.components()[0]) == utf16);
}

/* --- Ordering --------------------------------------------------------- */

TEST(logicalPath, aDirectorySortsImmediatelyBeforeItsChildren)
{
    /* `CanonPath::operator<=>` documents this property; byte-wise
       comparison of the joined form does not have it, because `!` is
       greater than `/`. */
    auto foo = LogicalPath::parseNative("/foo");
    auto fooBar = LogicalPath::parseNative("/foo/bar");
    auto fooBang = LogicalPath::parseNative("/foo!");

    EXPECT_LT(foo, fooBar);
    EXPECT_LT(fooBar, fooBang);
}

TEST(logicalPath, differentVolumesDoNotInterleave)
{
    auto c = LogicalPath::parseNative("C:\\z");
    auto d = LogicalPath::parseNative("D:\\a");
    EXPECT_LT(c, d);
}

/* --- Rejections ------------------------------------------------------- */

TEST(logicalPath, aForeignSchemeIsRejected)
{
    EXPECT_THROW(LogicalPath::parseUri("https://example.com/a"), BadLogicalPath);
}

TEST(logicalPath, nulInAComponentIsRejected)
{
    EXPECT_THROW(LogicalPath(LogicalPath::Posix{}, {std::string("a\0b", 3)}), BadLogicalPath);
}

/* --- The upstream blocker this design runs into ----------------------- */

TEST(logicalPath, nixCannotYetParseTheRfc8089UncForm)
{
    /* `url.cc:184` rejects any `file` URL with a non-empty host, so the
       one mapping RFC 8089 appendix E.3.1 gives for a UNC path is not
       parseable by Nix today. `LogicalPath::parseUri` therefore does its
       own scheme/authority split.

       This test exists to make the blocker visible and to fail loudly if
       the restriction is ever lifted, at which point `parseUri` should
       go back to using `parseURL`. */
    EXPECT_THROW(parseURL("file://host.example.com/Share/f"), BadURL);

    /* Whereas the same path does round-trip through this type. */
    auto p = LogicalPath::parseNative("\\\\host.example.com\\Share\\f");
    EXPECT_EQ(LogicalPath::parseUri(p.toUri()), p);
}

} // namespace nix
