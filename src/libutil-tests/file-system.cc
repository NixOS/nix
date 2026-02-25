#include "nix/util/util.hh"
#include "nix/util/serialise.hh"
#include "nix/util/types.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/terminal.hh"
#include "nix/util/strings.hh"

#include <limits.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <numeric>

using namespace std::string_view_literals;

#ifdef _WIN32
#  define FS_SEP L"\\"
#  define FS_ROOT_NO_TRAILING_SLASH L"C:" // Need a mounted one, C drive is likely
#  define FS_ROOT FS_ROOT_NO_TRAILING_SLASH FS_SEP
#else
#  define FS_SEP "/"
#  define FS_ROOT_NO_TRAILING_SLASH FS_SEP
#  define FS_ROOT FS_ROOT_NO_TRAILING_SLASH
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

    ASSERT_EQ(p, FS_ROOT_NO_TRAILING_SLASH);
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

    auto cwdPath = std::filesystem::path{cwd};
    auto p = absPath("", &cwdPath);

    ASSERT_EQ(p, cwdPath);
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
    ASSERT_ANY_THROW(canonPath("."sv));
    ASSERT_ANY_THROW(canonPath(".."sv));
    ASSERT_ANY_THROW(canonPath("../"sv));
    ASSERT_DEATH({ canonPath(""sv); }, "!path.empty\\(\\)");
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
    EXPECT_TRUE(isInDir(FS_ROOT "foo" FS_SEP "bar", FS_ROOT "foo"));
}

TEST(isInDir, notInDir)
{
    EXPECT_FALSE(isInDir(FS_ROOT "zes" FS_SEP "foo" FS_SEP "bar", FS_ROOT "foo"));
}

TEST(isInDir, emptyDir)
{
    EXPECT_FALSE(isInDir(FS_ROOT "zes" FS_SEP "foo" FS_SEP "bar", ""));
}

TEST(isInDir, hiddenSubdirectory)
{
    EXPECT_TRUE(isInDir(FS_ROOT "foo" FS_SEP ".ssh", FS_ROOT "foo"));
}

TEST(isInDir, ellipsisEntry)
{
    EXPECT_TRUE(isInDir(FS_ROOT "foo" FS_SEP "...", FS_ROOT "foo"));
}

TEST(isInDir, sameDir)
{
    EXPECT_FALSE(isInDir(FS_ROOT "foo", FS_ROOT "foo"));
}

TEST(isInDir, sameDirDot)
{
    EXPECT_FALSE(isInDir(FS_ROOT "foo" FS_SEP ".", FS_ROOT "foo"));
}

TEST(isInDir, dotDotPrefix)
{
    EXPECT_FALSE(isInDir(FS_ROOT "foo" FS_SEP ".." FS_SEP "bar", FS_ROOT "foo"));
}

/* ----------------------------------------------------------------------------
 * isDirOrInDir
 * --------------------------------------------------------------------------*/

TEST(isDirOrInDir, trueForSameDirectory)
{
    ASSERT_EQ(isDirOrInDir(FS_ROOT "nix", FS_ROOT "nix"), true);
    ASSERT_EQ(isDirOrInDir(FS_ROOT, FS_ROOT), true);
}

TEST(isDirOrInDir, trueForEmptyPaths)
{
    ASSERT_EQ(isDirOrInDir("", ""), true);
}

TEST(isDirOrInDir, falseForDisjunctPaths)
{
    ASSERT_EQ(isDirOrInDir(FS_ROOT "foo", FS_ROOT "bar"), false);
}

TEST(isDirOrInDir, relativePaths)
{
    ASSERT_EQ(isDirOrInDir(FS_ROOT "foo" FS_SEP "..", FS_ROOT "foo"), false);
}

TEST(isDirOrInDir, relativePathsTwice)
{
    ASSERT_EQ(isDirOrInDir(FS_ROOT "foo" FS_SEP "..", FS_ROOT "foo" FS_SEP "."), false);
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
    ASSERT_EQ(makeParentCanonical(FS_ROOT), FS_ROOT_NO_TRAILING_SLASH);
}

/* ----------------------------------------------------------------------------
 * chmodIfNeeded
 * --------------------------------------------------------------------------*/

TEST(chmodIfNeeded, works)
{
#ifdef _WIN32
    // Windows doesn't support Unix-style permission bits - lstat always
    // returns the same mode regardless of what chmod sets.
    GTEST_SKIP() << "Broken on Windows";
#endif
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
    ASSERT_THROW(chmodIfNeeded("/schnitzel/darmstadt/pommes", 0755), SystemError);
}

/* ----------------------------------------------------------------------------
 * DirectoryIterator
 * --------------------------------------------------------------------------*/

TEST(DirectoryIterator, works)
{
    auto tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, true);

    nix::writeFile(tmpDir / "somefile", "");

    for (auto path : DirectoryIterator(tmpDir)) {
        ASSERT_EQ(path.path(), tmpDir / "somefile");
    }
}

TEST(DirectoryIterator, nonexistent)
{
    ASSERT_THROW(DirectoryIterator("/schnitzel/darmstadt/pommes"), SystemError);
}

/* ----------------------------------------------------------------------------
 * createAnonymousTempFile
 * --------------------------------------------------------------------------*/

TEST(createAnonymousTempFile, works)
{
    auto fd = createAnonymousTempFile();
    writeFull(fd.get(), "test");
    lseek(fd.get(), 0, SEEK_SET);
    FdSource source{fd.get()};
    EXPECT_EQ(source.drain(), "test");
    lseek(fd.get(), 0, SEEK_END);
    writeFull(fd.get(), "test");
    lseek(fd.get(), 0, SEEK_SET);
    EXPECT_EQ(source.drain(), "testtest");
}

/* ----------------------------------------------------------------------------
 * FdSource
 * --------------------------------------------------------------------------*/

TEST(FdSource, restartWorks)
{
    auto fd = createAnonymousTempFile();
    writeFull(fd.get(), "hello world");
    lseek(fd.get(), 0, SEEK_SET);
    FdSource source{fd.get()};
    EXPECT_EQ(source.drain(), "hello world");
    source.restart();
    EXPECT_EQ(source.drain(), "hello world");
    EXPECT_EQ(source.drain(), "");
}

TEST(createTempDir, works)
{
    auto tmpDir = createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);
    ASSERT_TRUE(std::filesystem::is_directory(tmpDir));
}

} // namespace nix
