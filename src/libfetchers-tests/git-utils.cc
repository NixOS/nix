#include "git-utils.hh"
#include "file-system.hh"
#include "gmock/gmock.h"
#include <git2/global.h>
#include <git2/repository.h>
#include <git2/types.h>
#include <gtest/gtest.h>
#include "fs-sink.hh"
#include "serialise.hh"

namespace nix {

class GitUtilsTest : public ::testing::Test
{
    // We use a single repository for all tests.
    Path tmpDir;
    std::unique_ptr<AutoDelete> delTmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);

        // Create the repo with libgit2
        git_libgit2_init();
        git_repository * repo = nullptr;
        auto r = git_repository_init(&repo, tmpDir.c_str(), 0);
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
        return GitRepo::openRepo(tmpDir, true, false);
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
    auto accessor = repo->getAccessor(result, false);
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), 5);
    ASSERT_EQ(accessor->readFile(CanonPath("hello")), "hello world");
    ASSERT_EQ(accessor->readFile(CanonPath("bye")), "thanks for all the fish");
    ASSERT_EQ(accessor->readLink(CanonPath("bye-link")), "bye");
    ASSERT_EQ(accessor->readDirectory(CanonPath("empty")).size(), 0);
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
        FAIL() << "Expected an exception";
    } catch (const nix::Error & e) {
        ASSERT_THAT(e.msg(), testing::HasSubstr("cannot find hard link target"));
        ASSERT_THAT(e.msg(), testing::HasSubstr("/hello"));
        ASSERT_THAT(e.msg(), testing::HasSubstr("foo-1.1/link"));
    }
};

} // namespace nix
