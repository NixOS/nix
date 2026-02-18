#include "nix/fetchers/git-utils.hh"
#include "nix/util/file-system.hh"
#include <gmock/gmock.h>
#include <git2/global.h>
#include <git2/repository.h>
#include <git2/signature.h>
#include <git2/types.h>
#include <git2/object.h>
#include <git2/tag.h>
#include <gtest/gtest.h>
#include "nix/util/fs-sink.hh"
#include "nix/util/serialise.hh"
#include "nix/fetchers/git-lfs-fetch.hh"

#include <git2/blob.h>
#include <git2/tree.h>
#include <fstream>

namespace nix {

class GitUtilsTest : public ::testing::Test
{
    // We use a single repository for all tests.
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);

        // Create the repo with libgit2
        git_libgit2_init();
        git_repository * repo = nullptr;
        auto r = git_repository_init(&repo, tmpDir.string().c_str(), 0);
        ASSERT_EQ(r, 0);
        git_repository_free(repo);
    }

    void TearDown() override
    {
        // Destroy the AutoDelete, triggering removal
        // not AutoDelete::reset(), which would cancel the deletion.
        delTmpDir.reset();
    }

    ref<GitRepo> openRepo()
    {
        return GitRepo::openRepo(tmpDir, {.create = true});
    }

    std::string getRepoName() const
    {
        return tmpDir.filename().string();
    }
};

void writeString(CreateRegularFileSink & fileSink, std::string contents, bool executable)
{
    if (executable)
        fileSink.isExecutable();
    fileSink.preallocateContents(contents.size());
    fileSink(contents);
}

TEST_F(GitUtilsTest, sink_basic)
{
    auto repo = openRepo();
    auto sink = repo->getFileSystemObjectSink();

    // TODO/Question: It seems a little odd that we use the tarball-like convention of requiring a top-level directory
    // here
    //                The sync method does not document this behavior, should probably renamed because it's not very
    //                general, and I can't imagine that "non-conventional" archives or any other source to be handled by
    //                this sink.

    sink->createDirectory(CanonPath("foo-1.1"));

    sink->createRegularFile(CanonPath("foo-1.1/hello"), [](CreateRegularFileSink & fileSink) {
        writeString(fileSink, "hello world", false);
    });
    sink->createRegularFile(CanonPath("foo-1.1/bye"), [](CreateRegularFileSink & fileSink) {
        writeString(fileSink, "thanks for all the fish", false);
    });
    sink->createSymlink(CanonPath("foo-1.1/bye-link"), "bye");
    sink->createDirectory(CanonPath("foo-1.1/empty"));
    sink->createDirectory(CanonPath("foo-1.1/links"));
    sink->createHardlink(CanonPath("foo-1.1/links/foo"), CanonPath("foo-1.1/hello"));

    // sink->createHardlink("foo-1.1/links/foo-2", CanonPath("foo-1.1/hello"));

    auto result = repo->dereferenceSingletonDirectory(sink->flush());
    auto accessor = repo->getAccessor(result, {}, getRepoName());
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), 5u);
    ASSERT_EQ(accessor->readFile(CanonPath("hello")), "hello world");
    ASSERT_EQ(accessor->readFile(CanonPath("bye")), "thanks for all the fish");
    ASSERT_EQ(accessor->readLink(CanonPath("bye-link")), "bye");
    ASSERT_EQ(accessor->readDirectory(CanonPath("empty")).size(), 0u);
    ASSERT_EQ(accessor->readFile(CanonPath("links/foo")), "hello world");
};

TEST_F(GitUtilsTest, sink_hardlink)
{
    auto repo = openRepo();
    auto sink = repo->getFileSystemObjectSink();

    sink->createDirectory(CanonPath("foo-1.1"));

    sink->createRegularFile(CanonPath("foo-1.1/hello"), [](CreateRegularFileSink & fileSink) {
        writeString(fileSink, "hello world", false);
    });

    try {
        sink->createHardlink(CanonPath("foo-1.1/link"), CanonPath("hello"));
        sink->flush();
        FAIL() << "Expected an exception";
    } catch (const nix::Error & e) {
        ASSERT_THAT(e.msg(), testing::HasSubstr("does not exist"));
        ASSERT_THAT(e.msg(), testing::HasSubstr("/hello"));
        ASSERT_THAT(e.msg(), testing::HasSubstr("foo-1.1/link"));
    }
};

TEST_F(GitUtilsTest, peel_reference)
{
    // Create a commit in the repo
    git_repository * rawRepo = nullptr;
    ASSERT_EQ(git_repository_open(&rawRepo, tmpDir.string().c_str()), 0);

    // Create a blob
    git_oid blob_oid;
    const char * blob_content = "hello world";
    ASSERT_EQ(git_blob_create_from_buffer(&blob_oid, rawRepo, blob_content, strlen(blob_content)), 0);

    // Create a tree with that blob
    git_treebuilder * builder = nullptr;
    ASSERT_EQ(git_treebuilder_new(&builder, rawRepo, nullptr), 0);
    ASSERT_EQ(git_treebuilder_insert(nullptr, builder, "file.txt", &blob_oid, GIT_FILEMODE_BLOB), 0);

    git_oid tree_oid;
    ASSERT_EQ(git_treebuilder_write(&tree_oid, builder), 0);
    git_treebuilder_free(builder);

    git_tree * tree = nullptr;
    ASSERT_EQ(git_tree_lookup(&tree, rawRepo, &tree_oid), 0);

    // Create a commit
    git_signature * sig = nullptr;
    ASSERT_EQ(git_signature_now(&sig, "nix", "nix@example.com"), 0);

    git_oid commit_oid;
    ASSERT_EQ(git_commit_create_v(&commit_oid, rawRepo, "HEAD", sig, sig, nullptr, "initial commit", tree, 0), 0);

    // Lookup our commit
    git_object * commit_object = nullptr;
    ASSERT_EQ(git_object_lookup(&commit_object, rawRepo, &commit_oid, GIT_OBJECT_COMMIT), 0);

    // Create annotated tag
    git_oid tag_oid;
    ASSERT_EQ(git_tag_create(&tag_oid, rawRepo, "v1", commit_object, sig, "annotated tag", 0), 0);

    auto repo = openRepo();

    // Use resolveRef to get peeled object
    auto resolved = repo->resolveRef("refs/tags/v1");

    // Now assert that we have unpeeled it!
    ASSERT_STREQ(resolved.gitRev().c_str(), git_oid_tostr_s(&commit_oid));

    git_signature_free(sig);
    git_repository_free(rawRepo);
}

// ============================================================================
// Tests for getSubtreeSha (Phase 2)
// ============================================================================

TEST_F(GitUtilsTest, getSubtreeSha_finds_entry)
{
    git_repository * rawRepo = nullptr;
    ASSERT_EQ(git_repository_open(&rawRepo, tmpDir.string().c_str()), 0);

    // Create a blob for file content
    git_oid blob_oid;
    const char * blob_content = "file content";
    ASSERT_EQ(git_blob_create_from_buffer(&blob_oid, rawRepo, blob_content, strlen(blob_content)), 0);

    // Create inner tree (subdir)
    git_treebuilder * innerBuilder = nullptr;
    ASSERT_EQ(git_treebuilder_new(&innerBuilder, rawRepo, nullptr), 0);
    ASSERT_EQ(git_treebuilder_insert(nullptr, innerBuilder, "file.txt", &blob_oid, GIT_FILEMODE_BLOB), 0);

    git_oid innerTreeOid;
    ASSERT_EQ(git_treebuilder_write(&innerTreeOid, innerBuilder), 0);
    git_treebuilder_free(innerBuilder);

    // Create outer tree with subdir
    git_treebuilder * outerBuilder = nullptr;
    ASSERT_EQ(git_treebuilder_new(&outerBuilder, rawRepo, nullptr), 0);
    ASSERT_EQ(git_treebuilder_insert(nullptr, outerBuilder, "subdir", &innerTreeOid, GIT_FILEMODE_TREE), 0);

    git_oid outerTreeOid;
    ASSERT_EQ(git_treebuilder_write(&outerTreeOid, outerBuilder), 0);
    git_treebuilder_free(outerBuilder);

    git_repository_free(rawRepo);

    // Now test getSubtreeSha
    auto repo = openRepo();

    char outerSha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(outerSha, sizeof(outerSha), &outerTreeOid);
    auto outerHash = Hash::parseNonSRIUnprefixed(outerSha, HashAlgorithm::SHA1);

    char innerSha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(innerSha, sizeof(innerSha), &innerTreeOid);
    auto expectedInnerHash = Hash::parseNonSRIUnprefixed(innerSha, HashAlgorithm::SHA1);

    auto resultHash = repo->getSubtreeSha(outerHash, "subdir");
    ASSERT_EQ(resultHash, expectedInnerHash);
}

TEST_F(GitUtilsTest, getSubtreeSha_missing_entry_throws)
{
    git_repository * rawRepo = nullptr;
    ASSERT_EQ(git_repository_open(&rawRepo, tmpDir.string().c_str()), 0);

    // Create empty tree
    git_treebuilder * builder = nullptr;
    ASSERT_EQ(git_treebuilder_new(&builder, rawRepo, nullptr), 0);

    // Add a dummy entry so tree isn't empty
    git_oid blob_oid;
    const char * blob_content = "x";
    ASSERT_EQ(git_blob_create_from_buffer(&blob_oid, rawRepo, blob_content, strlen(blob_content)), 0);
    ASSERT_EQ(git_treebuilder_insert(nullptr, builder, "existing", &blob_oid, GIT_FILEMODE_BLOB), 0);

    git_oid treeOid;
    ASSERT_EQ(git_treebuilder_write(&treeOid, builder), 0);
    git_treebuilder_free(builder);
    git_repository_free(rawRepo);

    auto repo = openRepo();

    char sha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(sha, sizeof(sha), &treeOid);
    auto treeHash = Hash::parseNonSRIUnprefixed(sha, HashAlgorithm::SHA1);

    ASSERT_THROW(repo->getSubtreeSha(treeHash, "nonexistent"), Error);
}

// ============================================================================
// Tests for getCommitTree (Phase 2)
// ============================================================================

TEST_F(GitUtilsTest, getCommitTree_returns_root_tree)
{
    git_repository * rawRepo = nullptr;
    ASSERT_EQ(git_repository_open(&rawRepo, tmpDir.string().c_str()), 0);

    // Create a blob
    git_oid blob_oid;
    const char * blob_content = "content";
    ASSERT_EQ(git_blob_create_from_buffer(&blob_oid, rawRepo, blob_content, strlen(blob_content)), 0);

    // Create a tree
    git_treebuilder * builder = nullptr;
    ASSERT_EQ(git_treebuilder_new(&builder, rawRepo, nullptr), 0);
    ASSERT_EQ(git_treebuilder_insert(nullptr, builder, "file.txt", &blob_oid, GIT_FILEMODE_BLOB), 0);

    git_oid treeOid;
    ASSERT_EQ(git_treebuilder_write(&treeOid, builder), 0);
    git_treebuilder_free(builder);

    git_tree * tree = nullptr;
    ASSERT_EQ(git_tree_lookup(&tree, rawRepo, &treeOid), 0);

    // Create a commit
    git_signature * sig = nullptr;
    ASSERT_EQ(git_signature_now(&sig, "test", "test@example.com"), 0);

    git_oid commitOid;
    ASSERT_EQ(git_commit_create_v(&commitOid, rawRepo, "HEAD", sig, sig, nullptr, "test commit", tree, 0), 0);

    git_signature_free(sig);
    git_tree_free(tree);
    git_repository_free(rawRepo);

    // Now test getCommitTree
    auto repo = openRepo();

    char commitSha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(commitSha, sizeof(commitSha), &commitOid);
    auto commitHash = Hash::parseNonSRIUnprefixed(commitSha, HashAlgorithm::SHA1);

    char treeSha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(treeSha, sizeof(treeSha), &treeOid);
    auto expectedTreeHash = Hash::parseNonSRIUnprefixed(treeSha, HashAlgorithm::SHA1);

    auto resultHash = repo->getCommitTree(commitHash);
    ASSERT_EQ(resultHash, expectedTreeHash);
}

TEST_F(GitUtilsTest, getCommitTree_invalid_sha_throws)
{
    auto repo = openRepo();

    // Use a SHA that doesn't exist
    auto invalidHash = Hash::parseNonSRIUnprefixed(
        "0000000000000000000000000000000000000000", HashAlgorithm::SHA1);

    ASSERT_THROW(repo->getCommitTree(invalidHash), Error);
}

// ============================================================================
// Tests for hasObject
// ============================================================================

TEST_F(GitUtilsTest, hasObject_returns_true_for_existing)
{
    git_repository * rawRepo = nullptr;
    ASSERT_EQ(git_repository_open(&rawRepo, tmpDir.string().c_str()), 0);

    git_oid blob_oid;
    const char * blob_content = "test";
    ASSERT_EQ(git_blob_create_from_buffer(&blob_oid, rawRepo, blob_content, strlen(blob_content)), 0);
    git_repository_free(rawRepo);

    auto repo = openRepo();

    char sha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(sha, sizeof(sha), &blob_oid);
    auto hash = Hash::parseNonSRIUnprefixed(sha, HashAlgorithm::SHA1);

    ASSERT_TRUE(repo->hasObject(hash));
}

TEST_F(GitUtilsTest, hasObject_returns_false_for_missing)
{
    auto repo = openRepo();

    auto missingHash = Hash::parseNonSRIUnprefixed(
        "0000000000000000000000000000000000000000", HashAlgorithm::SHA1);

    ASSERT_FALSE(repo->hasObject(missingHash));
}

TEST(GitUtils, isLegalRefName)
{
    ASSERT_TRUE(isLegalRefName("A/b"));
    ASSERT_TRUE(isLegalRefName("AaA/b"));
    ASSERT_TRUE(isLegalRefName("FOO/BAR/BAZ"));
    ASSERT_TRUE(isLegalRefName("HEAD"));
    ASSERT_TRUE(isLegalRefName("refs/tags/1.2.3"));
    ASSERT_TRUE(isLegalRefName("refs/heads/master"));
    ASSERT_TRUE(isLegalRefName("foox"));
    ASSERT_TRUE(isLegalRefName("1337"));
    ASSERT_TRUE(isLegalRefName("foo.baz"));
    ASSERT_TRUE(isLegalRefName("foo/bar/baz"));
    ASSERT_TRUE(isLegalRefName("foo./bar"));
    ASSERT_TRUE(isLegalRefName("heads/foo@bar"));
    ASSERT_TRUE(isLegalRefName("heads/fu\303\237"));
    ASSERT_TRUE(isLegalRefName("foo-bar-baz"));
    ASSERT_TRUE(isLegalRefName("branch#"));
    ASSERT_TRUE(isLegalRefName("$1"));
    ASSERT_TRUE(isLegalRefName("foo.locke"));

    ASSERT_FALSE(isLegalRefName("refs///heads/foo"));
    ASSERT_FALSE(isLegalRefName("heads/foo/"));
    ASSERT_FALSE(isLegalRefName("///heads/foo"));
    ASSERT_FALSE(isLegalRefName(".foo"));
    ASSERT_FALSE(isLegalRefName("./foo"));
    ASSERT_FALSE(isLegalRefName("./foo/bar"));
    ASSERT_FALSE(isLegalRefName("foo/./bar"));
    ASSERT_FALSE(isLegalRefName("foo/bar/."));
    ASSERT_FALSE(isLegalRefName("foo bar"));
    ASSERT_FALSE(isLegalRefName("foo?bar"));
    ASSERT_FALSE(isLegalRefName("foo^bar"));
    ASSERT_FALSE(isLegalRefName("foo~bar"));
    ASSERT_FALSE(isLegalRefName("foo:bar"));
    ASSERT_FALSE(isLegalRefName("foo[bar"));
    ASSERT_FALSE(isLegalRefName("foo/bar/."));
    ASSERT_FALSE(isLegalRefName(".refs/foo"));
    ASSERT_FALSE(isLegalRefName("refs/heads/foo."));
    ASSERT_FALSE(isLegalRefName("heads/foo..bar"));
    ASSERT_FALSE(isLegalRefName("heads/foo?bar"));
    ASSERT_FALSE(isLegalRefName("heads/foo.lock"));
    ASSERT_FALSE(isLegalRefName("heads///foo.lock"));
    ASSERT_FALSE(isLegalRefName("foo.lock/bar"));
    ASSERT_FALSE(isLegalRefName("foo.lock///bar"));
    ASSERT_FALSE(isLegalRefName("heads/v@{ation"));
    ASSERT_FALSE(isLegalRefName("heads/foo\bar"));

    ASSERT_FALSE(isLegalRefName("@"));
    ASSERT_FALSE(isLegalRefName("\37"));
    ASSERT_FALSE(isLegalRefName("\177"));

    ASSERT_FALSE(isLegalRefName("foo/*"));
    ASSERT_FALSE(isLegalRefName("*/foo"));
    ASSERT_FALSE(isLegalRefName("foo/*/bar"));
    ASSERT_FALSE(isLegalRefName("*"));
    ASSERT_FALSE(isLegalRefName("foo/*/*"));
    ASSERT_FALSE(isLegalRefName("*/foo/*"));
    ASSERT_FALSE(isLegalRefName("/foo"));
    ASSERT_FALSE(isLegalRefName(""));
}

} // namespace nix
