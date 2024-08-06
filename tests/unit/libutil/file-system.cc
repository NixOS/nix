#include "util.hh"
#include "types.hh"
#include "file-system.hh"
#include "processes.hh"
#include "terminal.hh"
#include "strings.hh"

#include <limits.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <numeric>

#ifdef _WIN32
#  define FS_SEP "\\"
#  define FS_ROOT "C:" FS_SEP // Need a mounted one, C drive is likely
#else
#  define FS_SEP "/"
#  define FS_ROOT FS_SEP
#endif

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

namespace nix {

/* ----------- tests for file-system.hh -------------------------------------*/

/* ----------------------------------------------------------------------------
 * absPath
 * --------------------------------------------------------------------------*/

TEST(absPath, doesntChangeRoot)
{
    auto p = absPath(FS_ROOT);

    ASSERT_EQ(p, FS_ROOT);
}

TEST(absPath, turnsEmptyPathIntoCWD)
{
    char cwd[PATH_MAX + 1];
    auto p = absPath("");

    ASSERT_EQ(p, getcwd((char *) &cwd, PATH_MAX));
}

TEST(absPath, usesOptionalBasePathWhenGiven)
{
    char _cwd[PATH_MAX + 1];
    char * cwd = getcwd((char *) &_cwd, PATH_MAX);

    auto p = absPath("", cwd);

    ASSERT_EQ(p, cwd);
}

TEST(absPath, isIdempotent)
{
    char _cwd[PATH_MAX + 1];
    char * cwd = getcwd((char *) &_cwd, PATH_MAX);
    auto p1 = absPath(cwd);
    auto p2 = absPath(p1);

    ASSERT_EQ(p1, p2);
}

TEST(absPath, pathIsCanonicalised)
{
    auto path = FS_ROOT "some/path/with/trailing/dot/.";
    auto p1 = absPath(path);
    auto p2 = absPath(p1);

    ASSERT_EQ(p1, FS_ROOT "some" FS_SEP "path" FS_SEP "with" FS_SEP "trailing" FS_SEP "dot");
    ASSERT_EQ(p1, p2);
}

/* ----------------------------------------------------------------------------
 * canonPath
 * --------------------------------------------------------------------------*/

TEST(canonPath, removesTrailingSlashes)
{
    auto path = FS_ROOT "this/is/a/path//";
    auto p = canonPath(path);

    ASSERT_EQ(p, FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path");
}

TEST(canonPath, removesDots)
{
    auto path = FS_ROOT "this/./is/a/path/./";
    auto p = canonPath(path);

    ASSERT_EQ(p, FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path");
}

TEST(canonPath, removesDots2)
{
    auto path = FS_ROOT "this/a/../is/a////path/foo/..";
    auto p = canonPath(path);

    ASSERT_EQ(p, FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path");
}

TEST(canonPath, requiresAbsolutePath)
{
    ASSERT_ANY_THROW(canonPath("."));
    ASSERT_ANY_THROW(canonPath(".."));
    ASSERT_ANY_THROW(canonPath("../"));
    ASSERT_DEATH({ canonPath(""); }, "path != \"\"");
}

/* ----------------------------------------------------------------------------
 * dirOf
 * --------------------------------------------------------------------------*/

TEST(dirOf, returnsEmptyStringForRoot)
{
    auto p = dirOf("/");

    ASSERT_EQ(p, "/");
}

TEST(dirOf, returnsFirstPathComponent)
{
    auto p1 = dirOf("/dir/");
    ASSERT_EQ(p1, "/dir");
    auto p2 = dirOf("/dir");
    ASSERT_EQ(p2, "/");
    auto p3 = dirOf("/dir/..");
    ASSERT_EQ(p3, "/dir");
    auto p4 = dirOf("/dir/../");
    ASSERT_EQ(p4, "/dir/..");
}

/* ----------------------------------------------------------------------------
 * baseNameOf
 * --------------------------------------------------------------------------*/

TEST(baseNameOf, emptyPath)
{
    auto p1 = baseNameOf("");
    ASSERT_EQ(p1, "");
}

TEST(baseNameOf, pathOnRoot)
{
    auto p1 = baseNameOf("/dir");
    ASSERT_EQ(p1, "dir");
}

TEST(baseNameOf, relativePath)
{
    auto p1 = baseNameOf("dir/foo");
    ASSERT_EQ(p1, "foo");
}

TEST(baseNameOf, pathWithTrailingSlashRoot)
{
    auto p1 = baseNameOf("/");
    ASSERT_EQ(p1, "");
}

TEST(baseNameOf, trailingSlash)
{
    auto p1 = baseNameOf("/dir/");
    ASSERT_EQ(p1, "dir");
}

TEST(baseNameOf, trailingSlashes)
{
    auto p1 = baseNameOf("/dir//");
    ASSERT_EQ(p1, "dir");
}

TEST(baseNameOf, absoluteNothingSlashNothing)
{
    auto p1 = baseNameOf("//");
    ASSERT_EQ(p1, "");
}

/* ----------------------------------------------------------------------------
 * isInDir
 * --------------------------------------------------------------------------*/

TEST(isInDir, trivialCase)
{
    auto p1 = isInDir("/foo/bar", "/foo");
    ASSERT_EQ(p1, true);
}

TEST(isInDir, notInDir)
{
    auto p1 = isInDir("/zes/foo/bar", "/foo");
    ASSERT_EQ(p1, false);
}

// XXX: hm, bug or feature? :) Looking at the implementation
// this might be problematic.
TEST(isInDir, emptyDir)
{
    auto p1 = isInDir("/zes/foo/bar", "");
    ASSERT_EQ(p1, true);
}

/* ----------------------------------------------------------------------------
 * isDirOrInDir
 * --------------------------------------------------------------------------*/

TEST(isDirOrInDir, trueForSameDirectory)
{
    ASSERT_EQ(isDirOrInDir("/nix", "/nix"), true);
    ASSERT_EQ(isDirOrInDir("/", "/"), true);
}

TEST(isDirOrInDir, trueForEmptyPaths)
{
    ASSERT_EQ(isDirOrInDir("", ""), true);
}

TEST(isDirOrInDir, falseForDisjunctPaths)
{
    ASSERT_EQ(isDirOrInDir("/foo", "/bar"), false);
}

TEST(isDirOrInDir, relativePaths)
{
    ASSERT_EQ(isDirOrInDir("/foo/..", "/foo"), true);
}

// XXX: while it is possible to use "." or ".." in the
// first argument this doesn't seem to work in the second.
TEST(isDirOrInDir, DISABLED_shouldWork)
{
    ASSERT_EQ(isDirOrInDir("/foo/..", "/foo/."), true);
}

/* ----------------------------------------------------------------------------
 * pathExists
 * --------------------------------------------------------------------------*/

TEST(pathExists, rootExists)
{
    ASSERT_TRUE(pathExists(FS_ROOT));
}

TEST(pathExists, cwdExists)
{
    ASSERT_TRUE(pathExists("."));
}

TEST(pathExists, bogusPathDoesNotExist)
{
    ASSERT_FALSE(pathExists("/schnitzel/darmstadt/pommes"));
}
}
