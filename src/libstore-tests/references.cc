#include "nix/store/references.hh"
#include "nix/store/path-references.hh"
#include "nix/util/memory-source-accessor.hh"

#include <gtest/gtest.h>

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
    using File = MemorySourceAccessor::File;

    // Create store paths to search for
    StorePath path1{"dc04vv14dak1c1r48qa0m23vr9jy8sm0-foo"};
    StorePath path2{"zc842j0rz61mjsp3h3wp5ly71ak6qgdn-bar"};
    StorePath path3{"a5cn2i4b83gnsm60d38l3kgb8qfplm11-baz"};

    StorePathSet refs{path1, path2, path3};

    std::string_view hash1 = path1.hashPart();
    std::string_view hash2 = path2.hashPart();
    std::string_view hash3 = path3.hashPart();

    // Create an in-memory file system with various reference patterns
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->root = File::Directory{
        .contents{
            {
                // file1.txt: contains hash1
                "file1.txt",
                File::Regular{
                    .contents = "This file references " + hash1 + " in its content",
                },
            },
            {
                // file2.txt: contains hash2 and hash3
                "file2.txt",
                File::Regular{
                    .contents = "Multiple refs: " + hash2 + " and also " + hash3,
                },
            },
            {
                // file3.txt: contains no references
                "file3.txt",
                File::Regular{
                    .contents = "This file has no store path references at all",
                },
            },
            {
                // subdir: a subdirectory
                "subdir",
                File::Directory{
                    .contents{
                        {
                            // subdir/file4.txt: contains hash1 again
                            "file4.txt",
                            File::Regular{
                                .contents = "Subdirectory file with " + hash1,
                            },
                        },
                    },
                },
            },
            {
                // link1: a symlink that contains a reference in its target
                "link1",
                File::Symlink{
                    .target = hash2 + "-target",
                },
            },
        },
    };

    // Test the callback-based API
    {
        std::map<CanonPath, StorePathSet> foundRefs;

        scanForReferencesDeep(*accessor, CanonPath::root, refs, [&](FileRefScanResult result) {
            foundRefs[std::move(result.filePath)] = std::move(result.foundRefs);
        });

        // Verify we found the expected references
        EXPECT_EQ(foundRefs.size(), 4); // file1, file2, file4, link1

        // Check file1.txt found path1
        {
            CanonPath f1Path("/file1.txt");
            auto it = foundRefs.find(f1Path);
            ASSERT_TRUE(it != foundRefs.end());
            EXPECT_EQ(it->second.size(), 1);
            EXPECT_TRUE(it->second.count(path1));
        }

        // Check file2.txt found path2 and path3
        {
            CanonPath f2Path("/file2.txt");
            auto it = foundRefs.find(f2Path);
            ASSERT_TRUE(it != foundRefs.end());
            EXPECT_EQ(it->second.size(), 2);
            EXPECT_TRUE(it->second.count(path2));
            EXPECT_TRUE(it->second.count(path3));
        }

        // Check file3.txt is not in results (no refs)
        {
            CanonPath f3Path("/file3.txt");
            EXPECT_FALSE(foundRefs.count(f3Path));
        }

        // Check subdir/file4.txt found path1
        {
            CanonPath f4Path("/subdir/file4.txt");
            auto it = foundRefs.find(f4Path);
            ASSERT_TRUE(it != foundRefs.end());
            EXPECT_EQ(it->second.size(), 1);
            EXPECT_TRUE(it->second.count(path1));
        }

        // Check symlink found path2
        {
            CanonPath linkPath("/link1");
            auto it = foundRefs.find(linkPath);
            ASSERT_TRUE(it != foundRefs.end());
            EXPECT_EQ(it->second.size(), 1);
            EXPECT_TRUE(it->second.count(path2));
        }
    }

    // Test the map-based convenience API
    {
        auto results = scanForReferencesDeep(*accessor, CanonPath::root, refs);

        EXPECT_EQ(results.size(), 4); // file1, file2, file4, link1

        // Verify all expected files are in the results
        EXPECT_TRUE(results.count(CanonPath("/file1.txt")));
        EXPECT_TRUE(results.count(CanonPath("/file2.txt")));
        EXPECT_TRUE(results.count(CanonPath("/subdir/file4.txt")));
        EXPECT_TRUE(results.count(CanonPath("/link1")));
        EXPECT_FALSE(results.count(CanonPath("/file3.txt")));

        // Verify the references found in each file are correct
        EXPECT_EQ(results.at(CanonPath("/file1.txt")), StorePathSet{path1});
        EXPECT_EQ(results.at(CanonPath("/file2.txt")), StorePathSet({path2, path3}));
        EXPECT_EQ(results.at(CanonPath("/subdir/file4.txt")), StorePathSet{path1});
        EXPECT_EQ(results.at(CanonPath("/link1")), StorePathSet{path2});
    }
}

} // namespace nix
