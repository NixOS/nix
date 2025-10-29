#include "nix/util/util.hh"
#include "nix/util/types.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/terminal.hh"
#include "nix/util/strings.hh"

#include <limits.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <numeric>

#ifdef _WIN32
#  define FS_SEP L"\\"
#  define FS_ROOT L"C:" FS_SEP // Need a mounted one, C drive is likely
#else
#  define FS_SEP "/"
#  define FS_ROOT FS_SEP
#endif

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

#ifdef _WIN32
#  define GET_CWD _wgetcwd
#else
#  define GET_CWD getcwd
#endif

namespace nix {

/* ----------- tests for file-system.hh -------------------------------------*/

/* ----------------------------------------------------------------------------
 * absPath
 * --------------------------------------------------------------------------*/

TEST(absPath, doesntChangeRoot)
{
    auto p = absPath(std::filesystem::path{FS_ROOT});

    ASSERT_EQ(p, FS_ROOT);
}

TEST(absPath, turnsEmptyPathIntoCWD)
{
    OsChar cwd[PATH_MAX + 1];
    auto p = absPath(std::filesystem::path{""});

    ASSERT_EQ(p, GET_CWD((OsChar *) &cwd, PATH_MAX));
}

TEST(absPath, usesOptionalBasePathWhenGiven)
{
    OsChar _cwd[PATH_MAX + 1];
    OsChar * cwd = GET_CWD((OsChar *) &_cwd, PATH_MAX);

    auto p = absPath(std::filesystem::path{""}.string(), std::filesystem::path{cwd}.string());

    ASSERT_EQ(p, std::filesystem::path{cwd}.string());
}

TEST(absPath, isIdempotent)
{
    OsChar _cwd[PATH_MAX + 1];
    OsChar * cwd = GET_CWD((OsChar *) &_cwd, PATH_MAX);
    auto p1 = absPath(std::filesystem::path{cwd});
    auto p2 = absPath(p1);

    ASSERT_EQ(p1, p2);
}

TEST(absPath, pathIsCanonicalised)
{
    auto path = FS_ROOT OS_STR("some/path/with/trailing/dot/.");
    auto p1 = absPath(std::filesystem::path{path});
    auto p2 = absPath(p1);

    ASSERT_EQ(p1, FS_ROOT "some" FS_SEP "path" FS_SEP "with" FS_SEP "trailing" FS_SEP "dot");
    ASSERT_EQ(p1, p2);
}

/* ----------------------------------------------------------------------------
 * canonPath
 * --------------------------------------------------------------------------*/

TEST(canonPath, removesTrailingSlashes)
{
    std::filesystem::path path = FS_ROOT "this/is/a/path//";
    auto p = canonPath(path.string());

    ASSERT_EQ(p, std::filesystem::path{FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path"}.string());
}

TEST(canonPath, removesDots)
{
    std::filesystem::path path = FS_ROOT "this/./is/a/path/./";
    auto p = canonPath(path.string());

    ASSERT_EQ(p, std::filesystem::path{FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path"}.string());
}

TEST(canonPath, removesDots2)
{
    std::filesystem::path path = FS_ROOT "this/a/../is/a////path/foo/..";
    auto p = canonPath(path.string());

    ASSERT_EQ(p, std::filesystem::path{FS_ROOT "this" FS_SEP "is" FS_SEP "a" FS_SEP "path"}.string());
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

TEST(isInDir, emptyDir)
{
    auto p1 = isInDir("/zes/foo/bar", "");
    ASSERT_EQ(p1, false);
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
    ASSERT_EQ(isDirOrInDir("/foo/..", "/foo"), false);
}

TEST(isDirOrInDir, relativePathsTwice)
{
    ASSERT_EQ(isDirOrInDir("/foo/..", "/foo/."), false);
}

/* ----------------------------------------------------------------------------
 * pathExists
 * --------------------------------------------------------------------------*/

TEST(pathExists, rootExists)
{
    ASSERT_TRUE(pathExists(std::filesystem::path{FS_ROOT}.string()));
}

TEST(pathExists, cwdExists)
{
    ASSERT_TRUE(pathExists("."));
}

TEST(pathExists, bogusPathDoesNotExist)
{
    ASSERT_FALSE(pathExists("/schnitzel/darmstadt/pommes"));
}

/* ----------------------------------------------------------------------------
 * makeParentCanonical
 * --------------------------------------------------------------------------*/

TEST(makeParentCanonical, noParent)
{
    ASSERT_EQ(makeParentCanonical("file"), absPath(std::filesystem::path("file")));
}

TEST(makeParentCanonical, root)
{
    ASSERT_EQ(makeParentCanonical("/"), "/");
}

/* ----------------------------------------------------------------------------
 * chmodIfNeeded
 * --------------------------------------------------------------------------*/

TEST(chmodIfNeeded, works)
{
    auto [autoClose_, tmpFile] = nix::createTempFile();
    auto deleteTmpFile = AutoDelete(tmpFile);

    auto modes = std::vector<mode_t>{0755, 0644, 0422, 0600, 0777};
    for (mode_t oldMode : modes) {
        for (mode_t newMode : modes) {
            chmod(tmpFile.c_str(), oldMode);
            bool permissionsChanged = false;
            ASSERT_NO_THROW(permissionsChanged = chmodIfNeeded(tmpFile, newMode));
            ASSERT_EQ(permissionsChanged, oldMode != newMode);
        }
    }
}

TEST(chmodIfNeeded, nonexistent)
{
    ASSERT_THROW(chmodIfNeeded("/schnitzel/darmstadt/pommes", 0755), SysError);
}

/* ----------------------------------------------------------------------------
 * DirectoryIterator
 * --------------------------------------------------------------------------*/

TEST(DirectoryIterator, works)
{
    auto tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, true);

    nix::writeFile(tmpDir + "/somefile", "");

    for (auto path : DirectoryIterator(tmpDir)) {
        ASSERT_EQ(path.path().string(), tmpDir + "/somefile");
    }
}

TEST(DirectoryIterator, nonexistent)
{
    ASSERT_THROW(DirectoryIterator("/schnitzel/darmstadt/pommes"), SysError);
}

} // namespace nix
