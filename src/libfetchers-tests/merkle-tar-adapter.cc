#include "nix/fetchers/git-utils.hh"
#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/file-system.hh"
#include "nix/util/serialise.hh"
#include "nix/util/tests/capture-logging.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

namespace nix {

class MerkleTarAdapterTest : public ::testing::Test
{
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir() / "test-git-repo";
        /* Create the repo, and discard the handle: the tests open their
           own. */
        GitRepo::openRepo(tmpDir, {.create = true});
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }

    ref<GitRepo> openRepo()
    {
        return GitRepo::openRepo(tmpDir, {.create = false});
    }

    ref<GitRepoPool> openWriterPool()
    {
        return GitRepoPool::create(tmpDir, {.create = false});
    }

    std::string getRepoName() const
    {
        return tmpDir.filename().string();
    }
};

TEST_F(MerkleTarAdapterTest, empty_archive)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* An archive with no entries warns, but still gives an empty
       directory: `downloadTarball` cannot tell that the server answered
       "not modified" until it has already unpacked the (empty) response,
       and then throws the result away. */
    std::optional<merkle::TreeEntry> result;
    {
        testing::CaptureLogging log;
        result = tarSink->flush();
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("tar archive is empty"));
    }
    ASSERT_EQ(result->mode, merkle::Mode::Directory);

    auto accessor = repo->getAccessor(result->hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, single_file_at_root)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath::root, false, [](Sink & sink) { sink("hello world"); });

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Regular);

    // Wrap in a directory to verify content via accessor
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("file", result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("file"), "hello world"));
}

TEST_F(MerkleTarAdapterTest, single_executable_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("script.sh"), true, [](Sink & sink) { sink("#!/bin/bash\necho hello"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("script.sh"), "#!/bin/bash\necho hello"));
}

TEST_F(MerkleTarAdapterTest, single_symlink)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath::root, "target");

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Symlink);

    // Wrap in a directory to verify content via accessor
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link", result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("link"), "target"));
}

TEST_F(MerkleTarAdapterTest, empty_directory)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath::root);

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Directory);

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, nested_empty_directories)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("a/b/c"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("a"), std::set<std::string>{"b"}));
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("a/b"), std::set<std::string>{"c"}));
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("a/b/c"), std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, directory_with_files)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("hello.txt"), false, [](Sink & sink) { sink("hello world"); });
    tarSink->createRegularFile(CanonPath("bye.txt"), false, [](Sink & sink) { sink("goodbye"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{"hello.txt", "bye.txt"}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("hello.txt"), "hello world"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("bye.txt"), "goodbye"));
}

TEST_F(MerkleTarAdapterTest, nested_directory_with_files)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("a/b/file.txt"), false, [](Sink & sink) { sink("nested content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("a/b/file.txt"), "nested content"));
}

TEST_F(MerkleTarAdapterTest, mixed_content)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("file.txt"), false, [](Sink & sink) { sink("regular file"); });
    tarSink->createRegularFile(CanonPath("script.sh"), true, [](Sink & sink) { sink("#!/bin/bash"); });
    tarSink->createSymlink(CanonPath("link"), "file.txt");
    tarSink->createDirectory(CanonPath("empty"));
    tarSink->createRegularFile(CanonPath("subdir/nested.txt"), false, [](Sink & sink) { sink("nested"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(
        accessor,
        testing::HasDirectory(
            CanonPath::root, std::set<std::string>{"file.txt", "script.sh", "link", "empty", "subdir"}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("file.txt"), "regular file"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("script.sh"), "#!/bin/bash"));
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("link"), "file.txt"));
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("empty"), std::set<std::string>{}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("subdir/nested.txt"), "nested"));
}

TEST_F(MerkleTarAdapterTest, hardlink_target_not_found)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("link"), CanonPath("nonexistent")); },
        ::testing::ThrowsMessage<Error>(::testing::AllOf(
            testing::HasSubstrIgnoreANSIMatcher("does not exist"),
            testing::HasSubstrIgnoreANSIMatcher("nonexistent"),
            testing::HasSubstrIgnoreANSIMatcher("link"))));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("original.txt"), false, [](Sink & sink) { sink("shared content"); });
    tarSink->createHardlink(CanonPath("link.txt"), CanonPath("original.txt"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("original.txt"), "shared content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link.txt"), "shared content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_executable)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("script.sh"), true, [](Sink & sink) { sink("#!/bin/bash"); });
    tarSink->createHardlink(CanonPath("script-link.sh"), CanonPath("script.sh"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("script.sh"), "#!/bin/bash"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("script-link.sh"), "#!/bin/bash"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_symlink)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* `tar` allows this, and the result is two symlinks. */
    tarSink->createSymlink(CanonPath("s"), "some-target");
    tarSink->createHardlink(CanonPath("h"), CanonPath("s"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("s"), "some-target"));
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("h"), "some-target"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_symlink_replaced_by_a_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* Replacing one end with a different kind of thing is allowed, and
       leaves the other end as it was. */
    tarSink->createSymlink(CanonPath("s"), "some-target");
    tarSink->createHardlink(CanonPath("h"), CanonPath("s"));

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("s"), false, [](Sink & sink) { sink("no longer a symlink"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/h'"));
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("s"), "no longer a symlink"));
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("h"), "some-target"));
}

TEST_F(MerkleTarAdapterTest, multiple_hardlinks_same_target)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("original"), false, [](Sink & sink) { sink("content"); });
    tarSink->createHardlink(CanonPath("link1"), CanonPath("original"));
    tarSink->createHardlink(CanonPath("link2"), CanonPath("original"));
    tarSink->createHardlink(CanonPath("link3"), CanonPath("original"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("original"), "content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link1"), "content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link2"), "content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link3"), "content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_chain)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* A hard link may point at another hard link. This works because a
       link takes over its target's hash slot as it is created, so a chain
       collapses as it is built rather than needing to be followed. */
    constexpr int chainLength = 500;

    tarSink->createRegularFile(CanonPath("target"), false, [](Sink & sink) { sink("chained content"); });

    /* link0 -> target, link1 -> link0, ... */
    tarSink->createHardlink(CanonPath("link0"), CanonPath("target"));
    for (int i = 1; i < chainLength; i++)
        tarSink->createHardlink(CanonPath("link" + std::to_string(i)), CanonPath("link" + std::to_string(i - 1)));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("target"), "chained content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link0"), "chained content"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link" + std::to_string(chainLength - 1)), "chained content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_directory_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("dir"));

    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("link"), CanonPath("dir")); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_root_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* The old git sink had a `FIXME` here: it silently did nothing. */
    tarSink->createDirectory(CanonPath("foo"));

    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("bar"), CanonPath::root); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_later_path_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* The target has to be something the archive has already got to, so a
       hard link cannot form a cycle either. (libarchive only warns about
       a cyclic hard link; we reject it.) */
    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("foo"), CanonPath("later")); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("does not exist")));
}

TEST_F(MerkleTarAdapterTest, hardlink_keeps_the_file_it_was_made_from)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* Replacing the target afterwards does not follow through to the link:
       the link is to the file, not to the path. `tar` behaves this way
       because it unlinks the old file rather than writing through it. */
    tarSink->createRegularFile(CanonPath("target"), false, [](Sink & sink) { sink("old"); });
    tarSink->createHardlink(CanonPath("link"), CanonPath("target"));

    {
        /* Warned about, because older versions of Nix resolved the link
           against the replacement instead. */
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("target"), false, [](Sink & sink) { sink("new"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/link'"));
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("target"), "new"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link"), "old"));
}

TEST_F(MerkleTarAdapterTest, hardlink_replaced_itself_keeps_the_target)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* The other way round from `hardlink_keeps_the_file_it_was_made_from`:
       the link's own path is replaced, and the file it was made from is
       untouched. Warned about for the same reason. */
    tarSink->createRegularFile(CanonPath("target"), false, [](Sink & sink) { sink("original"); });
    tarSink->createHardlink(CanonPath("link"), CanonPath("target"));

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("link"), false, [](Sink & sink) { sink("replacement"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/target'"));
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("target"), "original"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("link"), "replacement"));
}

TEST_F(MerkleTarAdapterTest, hardlink_replaced_names_all_the_other_paths)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("a"), false, [](Sink & sink) { sink("shared"); });
    tarSink->createHardlink(CanonPath("b"), CanonPath("a"));
    tarSink->createHardlink(CanonPath("c"), CanonPath("a"));

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("a"), false, [](Sink & sink) { sink("replacement"); });
        /* All of them, not just the one it was linked from. */
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("'/b', '/c'"));
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("a"), "replacement"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("b"), "shared"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("c"), "shared"));
}

TEST_F(MerkleTarAdapterTest, hardlink_warns_only_while_paths_are_still_shared)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("a"), false, [](Sink & sink) { sink("shared"); });
    tarSink->createHardlink(CanonPath("b"), CanonPath("a"));

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("a"), false, [](Sink & sink) { sink("first"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as"));
    }

    {
        /* `b` is on its own now, so replacing it is unremarkable. */
        testing::CaptureLogging log;
        tarSink->createRegularFile(CanonPath("b"), false, [](Sink & sink) { sink("second"); });
        EXPECT_THAT(log.get(), ::testing::Not(testing::HasSubstrIgnoreANSIMatcher("the same file as")));
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("a"), "first"));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("b"), "second"));
}

TEST_F(MerkleTarAdapterTest, hardlink_failure_says_what_it_was_doing)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });

    /* The error itself is about `foo/bar`; the trace says why we were
       creating it. */
    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("foo/bar"), CanonPath("foo")); },
        ::testing::ThrowsMessage<Error>(::testing::AllOf(
            testing::HasSubstrIgnoreANSIMatcher("parent of '/foo/bar' is not a directory"),
            testing::HasSubstrIgnoreANSIMatcher("while creating a hard link from '/foo/bar' to '/foo'"))));
}

TEST_F(MerkleTarAdapterTest, child_of_file_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, symlink_child_of_file_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createSymlink(CanonPath("foo/bar"), "target"); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_child_of_file_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createHardlink(CanonPath("foo/bar"), CanonPath("foo")); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, child_of_symlink_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Symlink points to a valid directory, but we still can't create children under the symlink path
    tarSink->createDirectory(CanonPath("target"));
    tarSink->createSymlink(CanonPath("foo"), "target");

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, explicit_directory_replaces_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Create a file at "foo"
    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });
    // Explicitly replace it with a directory
    tarSink->createDirectory(CanonPath("foo"));
    // Now we can create children
    tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("child content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("foo/bar"), "child content"));
}

TEST_F(MerkleTarAdapterTest, large_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Create a file larger than the buffering threshold (1 MiB)
    std::string largeContent(2 * 1024 * 1024, 'x');

    tarSink->createRegularFile(CanonPath("large.bin"), false, [&](Sink & sink) { sink(largeContent); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("large.bin"), largeContent));
}

TEST_F(MerkleTarAdapterTest, replacing_empty_directory)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("foo"));
    tarSink->createDirectory(CanonPath("foo/bar"));
    /* Under tarball unpacking semantics, creating the same directories
       (implicitly or explicitly) is fine. */
    tarSink->createDirectory(CanonPath("foo/bar"));
    tarSink->createDirectory(CanonPath("foo"));

    tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("test"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("foo"), std::set<std::string>{"bar"}));
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("foo/bar"), "test"));
}

TEST_F(MerkleTarAdapterTest, replacing_non_empty_directory_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("foo"));
    tarSink->createDirectory(CanonPath("foo/bar"));

    /* This fails. libarchive (and other tarball unpackers) doesn't recursively unlink existing non-empty
       directories.
       https://github.com/libarchive/libarchive/blob/761652401fe35fca9744607a0cf0009afbf04f42/libarchive/archive_write_disk_posix.c#L3411-L3417
     */
    EXPECT_THAT(
        [&]() { tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("test"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("conflicting non-empty directory")));
}

TEST_F(MerkleTarAdapterTest, replacing_non_empty_directory_with_symlink_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("foo"));
    tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("test"); });

    EXPECT_THAT(
        [&]() { tarSink->createSymlink(CanonPath("foo"), "target"); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("conflicting non-empty directory")));
}

TEST_F(MerkleTarAdapterTest, child_of_hardlink_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("test"); });
    tarSink->createHardlink(CanonPath("bar"), CanonPath("foo"));

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile(CanonPath("bar/baz"), false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, many_files)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    constexpr int numFiles = 100;
    for (int i = 0; i < numFiles; i++) {
        auto path = CanonPath("file" + std::to_string(i) + ".txt");
        auto content = "content " + std::to_string(i);
        tarSink->createRegularFile(path, false, [&](Sink & sink) { sink(content); });
    }

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), static_cast<size_t>(numFiles));

    for (int i = 0; i < numFiles; i++) {
        auto path = CanonPath("file" + std::to_string(i) + ".txt");
        auto expectedContent = "content " + std::to_string(i);
        ASSERT_THAT(accessor, testing::HasContents(path, expectedContent));
    }
}

TEST_F(MerkleTarAdapterTest, deep_nesting)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(
        CanonPath("a/b/c/d/e/f/g/h/file.txt"), false, [](Sink & sink) { sink("deeply nested"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("a/b/c/d/e/f/g/h/file.txt"), "deeply nested"));
}

TEST_F(MerkleTarAdapterTest, directory_with_symlink_and_empty_subdir)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("file.txt"), false, [](Sink & sink) { sink("content"); });
    tarSink->createSymlink(CanonPath("link"), "file.txt");
    tarSink->createDirectory(CanonPath("empty"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("file.txt"), "content"));
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("link"), "file.txt"));
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath("empty"), std::set<std::string>{}));
}

// Tests for "last wins" tar semantics - when a path appears multiple times,
// the last entry should win.

TEST_F(MerkleTarAdapterTest, last_wins_file_overwrites_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("file.txt"), false, [](Sink & sink) { sink("first content"); });
    tarSink->createRegularFile(CanonPath("file.txt"), false, [](Sink & sink) { sink("second content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("file.txt"), "second content"));
}

TEST_F(MerkleTarAdapterTest, last_wins_file_overwrites_symlink)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath("entry"), "some-target");
    tarSink->createRegularFile(CanonPath("entry"), false, [](Sink & sink) { sink("file content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("entry"), "file content"));
}

TEST_F(MerkleTarAdapterTest, last_wins_symlink_overwrites_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("entry"), false, [](Sink & sink) { sink("file content"); });
    tarSink->createSymlink(CanonPath("entry"), "new-target");

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("entry"), "new-target"));
}

TEST_F(MerkleTarAdapterTest, last_wins_symlink_overwrites_symlink)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath("link"), "first-target");
    tarSink->createSymlink(CanonPath("link"), "second-target");

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("link"), "second-target"));
}

TEST_F(MerkleTarAdapterTest, last_wins_executable_changes)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("script"), false, [](Sink & sink) { sink("content"); });
    tarSink->createRegularFile(CanonPath("script"), true, [](Sink & sink) { sink("content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    // The file should be executable now (mode change)
    // We can verify content is there; checking executable bit would need different API
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("script"), "content"));
}

TEST_F(MerkleTarAdapterTest, last_wins_nested_file_overwrites)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("a/b/file.txt"), false, [](Sink & sink) { sink("first"); });
    tarSink->createRegularFile(CanonPath("a/b/file.txt"), false, [](Sink & sink) { sink("second"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasContents(CanonPath("a/b/file.txt"), "second"));
}

TEST_F(MerkleTarAdapterTest, last_wins_at_root)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath::root, "first-target");
    tarSink->createSymlink(CanonPath::root, "second-target");

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Symlink);

    // Wrap to verify
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link", result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_THAT(accessor, testing::HasSymlink(CanonPath("link"), "second-target"));
}

// Property-based test: last entry for each path wins
RC_GTEST_FIXTURE_PROP(MerkleTarAdapterTest, last_wins_property, ())
{
    // Generate a list of (path, content) pairs where paths may repeat
    auto pathNames = *rc::gen::container<std::vector<std::string>>(
        rc::gen::element(std::string("a"), std::string("b"), std::string("c")));

    RC_PRE(!pathNames.empty());

    auto entries = *rc::gen::container<std::vector<std::pair<std::string, std::string>>>(
        rc::gen::pair(rc::gen::elementOf(pathNames), rc::gen::arbitrary<std::string>()));

    RC_PRE(!entries.empty());

    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Track what the final content should be for each path
    std::map<std::string, std::string> expected;

    for (auto & [path, content] : entries) {
        tarSink->createRegularFile(CanonPath(path), false, [&](Sink & sink) { sink(content); });
        expected[path] = content; // Last write wins
    }

    auto result = tarSink->flush();
    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());

    // Verify each path has its expected final content
    for (auto & [path, content] : expected) {
        RC_ASSERT(accessor->readFile(CanonPath(path)) == content);
    }
}

} // namespace nix
