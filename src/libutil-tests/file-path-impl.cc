#include <gtest/gtest.h>

#include "nix/util/file-path-impl.hh"

namespace nix {

using WinChar = WindowsPathTrait<char>;

/* ----------------------------------------------------------------------------
 * Traditional DOS drives -- behaviour that predates UNC support
 * --------------------------------------------------------------------------*/

TEST(rootNameLen, driveLetter)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(C:\foo)"), 2u);
}

TEST(rootNameLen, driveLetterIsCaseInsensitive)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(c:\foo)"), 2u);
}

TEST(rootNameLen, bareDrive)
{
    EXPECT_EQ(WinChar::rootNameLen("C:"), 2u);
}

TEST(rootNameLen, nonLetterBeforeColonIsNotADrive)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(1:\foo)"), 0u);
}

TEST(rootNameLen, relativePathHasNoRoot)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(foo\bar)"), 0u);
}

TEST(rootNameLen, singleSeparatorHasNoRoot)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\foo)"), 0u);
}

/* ----------------------------------------------------------------------------
 * UNC shares. All of these returned 0 before, so canonicalisation treated
 * them as relative and could resolve `..` out of a share.
 * --------------------------------------------------------------------------*/

TEST(rootNameLen, uncShareWithTrailingPath)
{
    /* Root is `\\server\share`, 14 characters; `\dir` is inside it. */
    EXPECT_EQ(WinChar::rootNameLen(R"(\\server\share\dir)"), 14u);
}

TEST(rootNameLen, uncShareAlone)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\server\share)"), 14u);
}

TEST(rootNameLen, uncServerWithoutShareIsEntirelyRoot)
{
    /* There is no directory to be relative to, so the whole thing is the
       root name. A shorter answer would let `..` traverse into a sibling
       share. */
    EXPECT_EQ(WinChar::rootNameLen(R"(\\server)"), 8u);
}

TEST(rootNameLen, uncAcceptsForwardSlashes)
{
    /* `isPathSep` accepts both separators, so this must too. */
    EXPECT_EQ(WinChar::rootNameLen("//server/share/dir"), 14u);
}

/* ----------------------------------------------------------------------------
 * Extended-length and device namespaces
 * --------------------------------------------------------------------------*/

TEST(rootNameLen, extendedLengthDrive)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\?\C:\foo)"), 6u);
}

TEST(rootNameLen, extendedLengthDriveLowercase)
{
    /* `maybePath` accepted only an uppercase drive letter in this position
       while accepting either case in the plain `C:\` form. */
    EXPECT_EQ(WinChar::rootNameLen(R"(\\?\c:\foo)"), 6u);
}

TEST(rootNameLen, deviceNamespaceDrive)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\.\C:\foo)"), 6u);
}

TEST(rootNameLen, extendedLengthUnc)
{
    /* `\\?\UNC\server\share` is 20 characters. */
    EXPECT_EQ(WinChar::rootNameLen(R"(\\?\UNC\server\share\dir)"), 20u);
}

TEST(rootNameLen, extendedLengthUncIsCaseInsensitive)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\?\unc\server\share\dir)"), 20u);
}

TEST(rootNameLen, deviceName)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\.\PhysicalDrive0)"), 18u);
}

TEST(rootNameLen, deviceNameWithTrailingPath)
{
    EXPECT_EQ(WinChar::rootNameLen(R"(\\.\pipe\mypipe)"), 8u);
}

/* ----------------------------------------------------------------------------
 * The wide instantiation must agree. Before, `rootNameLen` narrowed the
 * drive letter to `char` regardless of `CharT`.
 * --------------------------------------------------------------------------*/

TEST(rootNameLen, wideCharAgreesWithNarrow)
{
    using WinWide = WindowsPathTrait<wchar_t>;
    EXPECT_EQ(WinWide::rootNameLen(LR"(C:\foo)"), 2u);
    EXPECT_EQ(WinWide::rootNameLen(LR"(\\server\share\dir)"), 14u);
    EXPECT_EQ(WinWide::rootNameLen(LR"(\\?\C:\foo)"), 6u);
    EXPECT_EQ(WinWide::rootNameLen(LR"(\\?\UNC\server\share)"), 20u);
}

/* ----------------------------------------------------------------------------
 * Unix paths have no root name, on any input
 * --------------------------------------------------------------------------*/

TEST(rootNameLen, unixTraitIsAlwaysZero)
{
    EXPECT_EQ(UnixPathTrait::rootNameLen("/foo/bar"), 0u);
    EXPECT_EQ(UnixPathTrait::rootNameLen("//server/share"), 0u);
    EXPECT_EQ(UnixPathTrait::rootNameLen("C:/foo"), 0u);
}

} // namespace nix
