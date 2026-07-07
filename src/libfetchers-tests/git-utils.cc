#include "nix/fetchers/git-utils.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/gmock-matchers.hh"

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

#include <git2/blob.h>
#include <git2/tree.h>

namespace nix::fetchers {

class GitUtilsTest : public ::testing::Test
{
    // We use a single repository for all tests.
    AutoDelete delTmpDir;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir() / "test-git-repo";
        GitRepo::openRepo(tmpDir, {.create = true});
        delTmpDir = AutoDelete(tmpDir, true);
    }

    void TearDown() override
    {
        delTmpDir.deletePath();
    }

    ref<GitRepo> openRepo()
    {
        return GitRepo::openRepo(tmpDir, {.create = false});
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

    ASSERT_THAT(
        accessor,
        testing::HasDirectory(
            CanonPath::root,
            std::set<std::string>{
                "hello",
                "bye",
                "bye-link",
                "empty",
                "links",
            }));

    ASSERT_THAT(accessor, testing::HasContents(CanonPath("hello"), "hello world"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("bye"), "thanks for all the fish"));
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("bye-link"), "bye"));
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("empty"), std::set<std::string>{}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("links/foo"), "hello world"));
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
        ASSERT_THAT(e.msg(), ::testing::HasSubstr("does not exist"));
        ASSERT_THAT(e.msg(), ::testing::HasSubstr("/hello"));
        ASSERT_THAT(e.msg(), ::testing::HasSubstr("foo-1.1/link"));
    }
};

TEST_F(GitUtilsTest, sink_no_parent_dir)
{
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);

            sink->createRegularFile(CanonPath("foo"), [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "test", /*executable=*/false);
            });

            sink->createRegularFile(CanonPath("foo/bar"), [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "boom", /*executable=*/false);
            });

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("parent of 'foo/bar' is not a directory")));
}

TEST_F(GitUtilsTest, sink_no_parent_dir_symlink)
{
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);

            sink->createRegularFile(CanonPath("foo"), [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "test", /*executable=*/false);
            });

            sink->createSymlink(CanonPath("foo/bar"), "target");

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("parent of 'foo/bar' is not a directory")));
}

TEST_F(GitUtilsTest, sink_no_parent_dir_hardlink)
{
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);

            sink->createRegularFile(CanonPath("foo"), [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "test", /*executable=*/false);
            });

            sink->createHardlink(CanonPath("foo/bar"), CanonPath("foo"));

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("parent of 'foo/bar' is not a directory")));
}

TEST_F(GitUtilsTest, sink_replacing_empty_directory)
{
    auto repo = openRepo();
    auto sink = repo->getFileSystemObjectSink();

    sink->createDirectory(CanonPath::root);
    sink->createDirectory(CanonPath("foo"));
    sink->createDirectory(CanonPath("foo/bar"));
    /* Under tarball unpacking semantics, creating the same directories
       (implicitly or explicitly) is fine. */
    sink->createDirectory(CanonPath("foo/bar"));
    sink->createDirectory(CanonPath("foo"));

    sink->createRegularFile(CanonPath("foo/bar"), [](CreateRegularFileSink & fileSink) {
        writeString(fileSink, "test", /*executable=*/false);
    });

    auto accessor = repo->getAccessor(sink->flush(), {}, getRepoName());

    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("foo"), std::set<std::string>{"bar"}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("foo/bar"), "test"));
}

TEST_F(GitUtilsTest, sink_replacing_non_empty_directory)
{
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);
            sink->createDirectory(CanonPath("foo"));
            sink->createDirectory(CanonPath("foo/bar"));

            /* This fails. libarchive (and other tarball unpackers) doesn't recursive unlink existing non-empty
               directories.
               https://github.com/libarchive/libarchive/blob/761652401fe35fca9744607a0cf0009afbf04f42/libarchive/archive_write_disk_posix.c#L3411-L3417
             */

            sink->createRegularFile(CanonPath("foo"), [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "test", /*executable=*/false);
            });

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("cannot create 'foo', conflicting non-empty directory")));
}

TEST_F(GitUtilsTest, sink_hardlink_to_directory)
{
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);
            sink->createDirectory(CanonPath("foo"));
            sink->createHardlink(CanonPath("bar"), CanonPath("foo"));

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("cannot create a hard link to a directory")));
}

TEST_F(GitUtilsTest, sink_hardlink_to_directory_root)
{
    auto repo = openRepo();
    auto sink = repo->getFileSystemObjectSink();

    sink->createDirectory(CanonPath::root);
    sink->createDirectory(CanonPath("foo"));
    sink->createHardlink(CanonPath("bar"), CanonPath::root);

    auto accessor = repo->getAccessor(sink->flush(), {}, getRepoName());

    /* FIXME: Why does it behave this way? This seems like a bug. */
    ASSERT_THAT(
        accessor,
        testing::HasDirectory(
            CanonPath::root,
            std::set<std::string>{
                "foo",
            }));
}

TEST_F(GitUtilsTest, sink_hardlink_to_self)
{
    /* Here we are more strict than libarchive, which only warns on cyclic hardlinks.
       https://github.com/libarchive/libarchive/blob/761652401fe35fca9744607a0cf0009afbf04f42/libarchive/archive_write_disk_posix.c#L632-L641
     */
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createDirectory(CanonPath::root);
            sink->createHardlink(CanonPath("foo"), CanonPath("foo"));

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("/foo")));
}

TEST_F(GitUtilsTest, sink_non_directory_root)
{
    /* FIXME: Allow non-directory roots. GitFileSystemObjectSink is too tarball-brained. */
    ASSERT_THAT(
        [&]() {
            auto repo = openRepo();
            auto sink = repo->getFileSystemObjectSink();

            sink->createRegularFile(CanonPath::root, [](CreateRegularFileSink & fileSink) {
                writeString(fileSink, "test", /*executable=*/false);
            });

            sink->flush();
        },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("cannot create a file at the root of the git repository")));
}

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

} // namespace nix::fetchers
