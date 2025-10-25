#include "nix/store/references.hh"
#include "nix/store/path-references.hh"
#include "nix/util/file-system.hh"
#include "nix/util/source-accessor.hh"

#include <gtest/gtest.h>
#include <fstream>

namespace nix {

struct RewriteParams
{
    std::string originalString, finalString;
    StringMap rewrites;

    friend std::ostream & operator<<(std::ostream & os, const RewriteParams & bar)
    {
        StringSet strRewrites;
        for (auto & [from, to] : bar.rewrites)
            strRewrites.insert(from + "->" + to);
        return os << "OriginalString: " << bar.originalString << std::endl
                  << "Rewrites: " << dropEmptyInitThenConcatStringsSep(",", strRewrites) << std::endl
                  << "Expected result: " << bar.finalString;
    }
};

class RewriteTest : public ::testing::TestWithParam<RewriteParams>
{};

TEST_P(RewriteTest, IdentityRewriteIsIdentity)
{
    RewriteParams param = GetParam();
    StringSink rewritten;
    auto rewriter = RewritingSink(param.rewrites, rewritten);
    rewriter(param.originalString);
    rewriter.flush();
    ASSERT_EQ(rewritten.s, param.finalString);
}

INSTANTIATE_TEST_CASE_P(
    references,
    RewriteTest,
    ::testing::Values(
        RewriteParams{"foooo", "baroo", {{"foo", "bar"}, {"bar", "baz"}}},
        RewriteParams{"foooo", "bazoo", {{"fou", "bar"}, {"foo", "baz"}}},
        RewriteParams{"foooo", "foooo", {}}));

TEST(references, scan)
{
    std::string hash1 = "dc04vv14dak1c1r48qa0m23vr9jy8sm0";
    std::string hash2 = "zc842j0rz61mjsp3h3wp5ly71ak6qgdn";

    {
        RefScanSink scanner(StringSet{hash1});
        auto s = "foobar";
        scanner(s);
        ASSERT_EQ(scanner.getResult(), StringSet{});
    }

    {
        RefScanSink scanner(StringSet{hash1});
        auto s = "foobar" + hash1 + "xyzzy";
        scanner(s);
        ASSERT_EQ(scanner.getResult(), StringSet{hash1});
    }

    {
        RefScanSink scanner(StringSet{hash1, hash2});
        auto s = "foobar" + hash1 + "xyzzy" + hash2;
        scanner(((std::string_view) s).substr(0, 10));
        scanner(((std::string_view) s).substr(10, 5));
        scanner(((std::string_view) s).substr(15, 5));
        scanner(((std::string_view) s).substr(20));
        ASSERT_EQ(scanner.getResult(), StringSet({hash1, hash2}));
    }

    {
        RefScanSink scanner(StringSet{hash1, hash2});
        auto s = "foobar" + hash1 + "xyzzy" + hash2;
        for (auto & i : s)
            scanner(std::string(1, i));
        ASSERT_EQ(scanner.getResult(), StringSet({hash1, hash2}));
    }
}

TEST(references, scanForReferencesDeep)
{
    // Create store paths to search for
    StorePath path1{"dc04vv14dak1c1r48qa0m23vr9jy8sm0-foo"};
    StorePath path2{"zc842j0rz61mjsp3h3wp5ly71ak6qgdn-bar"};
    StorePath path3{"a5cn2i4b83gnsm60d38l3kgb8qfplm11-baz"};

    StorePathSet refs{path1, path2, path3};

    std::string hash1 = std::string(path1.hashPart());
    std::string hash2 = std::string(path2.hashPart());
    std::string hash3 = std::string(path3.hashPart());

    // Create a temporary directory structure for testing
    Path tmpDirStr = createTempDir();
    AutoDelete delTmpDir(tmpDirStr);
    CanonPath tmpDir(tmpDirStr);

    // Create test files with various reference patterns
    {
        // file1.txt: contains hash1
        std::ofstream f1((tmpDir / "file1.txt").abs());
        f1 << "This file references " << hash1 << " in its content";
        f1.close();

        // file2.txt: contains hash2 and hash3
        std::ofstream f2((tmpDir / "file2.txt").abs());
        f2 << "Multiple refs: " << hash2 << " and also " << hash3;
        f2.close();

        // file3.txt: contains no references
        std::ofstream f3((tmpDir / "file3.txt").abs());
        f3 << "This file has no store path references at all";
        f3.close();

        // Create a subdirectory
        createDir((tmpDir / "subdir").abs());

        // subdir/file4.txt: contains hash1 again
        std::ofstream f4((tmpDir / "subdir" / "file4.txt").abs());
        f4 << "Subdirectory file with " << hash1;
        f4.close();

        // Create a symlink that contains a reference in its target
        std::filesystem::create_symlink(hash2 + "-target", (tmpDir / "link1").abs());
    }

    // Test the callback-based API
    {
        std::map<CanonPath, StorePathSet> foundRefs;
        auto accessor = getFSSourceAccessor();

        scanForReferencesDeep(*accessor, tmpDir, refs, [&](const FileRefScanResult & result) {
            foundRefs[result.filePath] = result.foundRefs;
        });

        // Verify we found the expected references
        ASSERT_EQ(foundRefs.size(), 4); // file1, file2, file4, link1

        // Check file1.txt found path1
        CanonPath f1Path = tmpDir / "file1.txt";
        ASSERT_TRUE(foundRefs.count(f1Path));
        ASSERT_EQ(foundRefs[f1Path].size(), 1);
        ASSERT_TRUE(foundRefs[f1Path].count(path1));

        // Check file2.txt found path2 and path3
        CanonPath f2Path = tmpDir / "file2.txt";
        ASSERT_TRUE(foundRefs.count(f2Path));
        ASSERT_EQ(foundRefs[f2Path].size(), 2);
        ASSERT_TRUE(foundRefs[f2Path].count(path2));
        ASSERT_TRUE(foundRefs[f2Path].count(path3));

        // Check file3.txt is not in results (no refs)
        CanonPath f3Path = tmpDir / "file3.txt";
        ASSERT_FALSE(foundRefs.count(f3Path));

        // Check subdir/file4.txt found path1
        CanonPath f4Path = tmpDir / "subdir" / "file4.txt";
        ASSERT_TRUE(foundRefs.count(f4Path));
        ASSERT_EQ(foundRefs[f4Path].size(), 1);
        ASSERT_TRUE(foundRefs[f4Path].count(path1));

        // Check symlink found path2
        CanonPath linkPath = tmpDir / "link1";
        ASSERT_TRUE(foundRefs.count(linkPath));
        ASSERT_EQ(foundRefs[linkPath].size(), 1);
        ASSERT_TRUE(foundRefs[linkPath].count(path2));
    }

    // Test the vector-based API
    {
        auto accessor = getFSSourceAccessor();
        auto results = scanForReferencesDeep(*accessor, tmpDir, refs);

        ASSERT_EQ(results.size(), 4); // file1, file2, file4, link1

        // Verify all expected files are in the results
        std::set<CanonPath> filePaths;
        for (auto & result : results) {
            filePaths.insert(result.filePath);
        }

        ASSERT_TRUE(filePaths.count(tmpDir / "file1.txt"));
        ASSERT_TRUE(filePaths.count(tmpDir / "file2.txt"));
        ASSERT_TRUE(filePaths.count(tmpDir / "subdir" / "file4.txt"));
        ASSERT_TRUE(filePaths.count(tmpDir / "link1"));
        ASSERT_FALSE(filePaths.count(tmpDir / "file3.txt"));
    }
}

} // namespace nix
