#include "nix/fetchers/git-utils.hh"
#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/file-system.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/tests/capture-logging.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

namespace nix {

class MerkleTarAdapterTest : public ::testing::Test
{
    AutoDelete delTmpDir;
    std::shared_ptr<GitRepo> repo;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir() / "test-git-repo";
        repo = GitRepo::openRepo(tmpDir, {.create = true});
        delTmpDir = AutoDelete(tmpDir, true);
    }

    void TearDown() override
    {
        repo.reset();
        delTmpDir.deletePath();
    }

    ref<merkle::FileSinkBuilder> openWriterPool()
    {
        /* TODO: Make this a parametrised test and have a mock in-memory content
           addressed store othen than git. */
        return GitRepoPool::create(tmpDir, {.create = false});
    }

    ref<SourceAccessor> getAccessor(const Hash & hash) const
    {
        return repo->getAccessor(hash, {}, tmpDir.filename().string());
    }
};

TEST_F(MerkleTarAdapterTest, empty_archive)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* An archive with no entries unpacks to an empty directory:
       `downloadTarball` cannot tell that the server answered "not modified"
       until it has already unpacked the (empty) response, and then throws the
       result away. */
    std::optional<merkle::TreeEntry> result;
    {
        testing::CaptureLogging log;
        result = tarSink->flush();
        EXPECT_EQ(log.get(), "");
    }
    ASSERT_EQ(result->mode, merkle::Mode::Directory);

    auto accessor = getAccessor(result->hash);
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, single_file_at_root)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath::root, false, [](Sink & sink) { sink("hello world"); });

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Regular);

    // Wrap in a directory to verify content via accessor
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("file"_filename, result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = getAccessor(dirHash);
    ASSERT_THAT(accessor, testing::HasContents("file"_filename, "hello world"));
}

TEST_F(MerkleTarAdapterTest, single_executable_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("script.sh"_filename, true, [](Sink & sink) { sink("#!/bin/bash\necho hello"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("script.sh"_filename, "#!/bin/bash\necho hello"));
    ASSERT_TRUE(accessor->lstat("script.sh"_filename).isExecutable);
}

TEST_F(MerkleTarAdapterTest, single_symlink)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath::root, "target");

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Symlink);

    // Wrap in a directory to verify content via accessor
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link"_filename, result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = getAccessor(dirHash);
    ASSERT_THAT(accessor, testing::HasSymlink("link"_filename, "target"));
}

TEST_F(MerkleTarAdapterTest, empty_directory)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath::root);

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Directory);

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, nested_empty_directories)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("/a/b/c"_canonPath);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasDirectory("a"_filename, std::set<std::string>{"b"}));
    ASSERT_THAT(accessor, testing::HasDirectory("/a/b"_canonPath, std::set<std::string>{"c"}));
    ASSERT_THAT(accessor, testing::HasDirectory("/a/b/c"_canonPath, std::set<std::string>{}));
}

TEST_F(MerkleTarAdapterTest, directory_with_files)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("hello.txt"_filename, false, [](Sink & sink) { sink("hello world"); });
    tarSink->createRegularFile("bye.txt"_filename, false, [](Sink & sink) { sink("goodbye"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasDirectory(CanonPath::root, std::set<std::string>{"hello.txt", "bye.txt"}));
    ASSERT_THAT(accessor, testing::HasContents("hello.txt"_filename, "hello world"));
    ASSERT_THAT(accessor, testing::HasContents("bye.txt"_filename, "goodbye"));
}

TEST_F(MerkleTarAdapterTest, nested_directory_with_files)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("/a/b/file.txt"_canonPath, false, [](Sink & sink) { sink("nested content"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("/a/b/file.txt"_canonPath, "nested content"));
}

TEST_F(MerkleTarAdapterTest, mixed_content)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("file.txt"_filename, false, [](Sink & sink) { sink("regular file"); });
    tarSink->createRegularFile("script.sh"_filename, true, [](Sink & sink) { sink("#!/bin/bash"); });
    tarSink->createSymlink("link"_filename, "file.txt");
    tarSink->createDirectory("empty"_filename);
    tarSink->createRegularFile("/subdir/nested.txt"_canonPath, false, [](Sink & sink) { sink("nested"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(
        accessor,
        testing::HasDirectory(
            CanonPath::root, std::set<std::string>{"file.txt", "script.sh", "link", "empty", "subdir"}));
    ASSERT_THAT(accessor, testing::HasContents("file.txt"_filename, "regular file"));
    ASSERT_THAT(accessor, testing::HasContents("script.sh"_filename, "#!/bin/bash"));
    ASSERT_THAT(accessor, testing::HasSymlink("link"_filename, "file.txt"));
    ASSERT_THAT(accessor, testing::HasDirectory("empty"_filename, std::set<std::string>{}));
    ASSERT_THAT(accessor, testing::HasContents("/subdir/nested.txt"_canonPath, "nested"));
}

TEST_F(MerkleTarAdapterTest, hardlink_target_not_found)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    EXPECT_THAT(
        [&]() { tarSink->createHardlink("link"_filename, "nonexistent"_filename); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("target does not exist")));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("original.txt"_filename, false, [](Sink & sink) { sink("shared content"); });
    tarSink->createHardlink("link.txt"_filename, "original.txt"_filename);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("original.txt"_filename, "shared content"));
    ASSERT_THAT(accessor, testing::HasContents("link.txt"_filename, "shared content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_executable)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("script.sh"_filename, true, [](Sink & sink) { sink("#!/bin/bash"); });
    tarSink->createHardlink("script-link.sh"_filename, "script.sh"_filename);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("script.sh"_filename, "#!/bin/bash"));
    ASSERT_THAT(accessor, testing::HasContents("script-link.sh"_filename, "#!/bin/bash"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_symlink)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* `tar` allows this, and the result is two symlinks. */
    tarSink->createSymlink("s"_filename, "some-target");
    tarSink->createHardlink("h"_filename, "s"_filename);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasSymlink("s"_filename, "some-target"));
    ASSERT_THAT(accessor, testing::HasSymlink("h"_filename, "some-target"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_symlink_replaced_by_a_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* Replacing one end with a different kind of thing is allowed, and
       leaves the other end as it was. */
    tarSink->createSymlink("s"_filename, "some-target");
    tarSink->createHardlink("h"_filename, "s"_filename);

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile("s"_filename, false, [](Sink & sink) { sink("no longer a symlink"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/h'"));
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("s"_filename, "no longer a symlink"));
    ASSERT_THAT(accessor, testing::HasSymlink("h"_filename, "some-target"));
}

TEST_F(MerkleTarAdapterTest, multiple_hardlinks_same_target)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("original"_filename, false, [](Sink & sink) { sink("content"); });
    tarSink->createHardlink("link1"_filename, "original"_filename);
    tarSink->createHardlink("link2"_filename, "original"_filename);
    tarSink->createHardlink("link3"_filename, "original"_filename);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("original"_filename, "content"));
    ASSERT_THAT(accessor, testing::HasContents("link1"_filename, "content"));
    ASSERT_THAT(accessor, testing::HasContents("link2"_filename, "content"));
    ASSERT_THAT(accessor, testing::HasContents("link3"_filename, "content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_chain)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* A hard link may point at another hard link. This works because a
       link takes over its target's hash slot as it is created, so a chain
       collapses as it is built rather than needing to be followed. */
    constexpr int chainLength = 500;

    tarSink->createRegularFile("target"_filename, false, [](Sink & sink) { sink("chained content"); });

    /* link0 -> target, link1 -> link0, ... */
    tarSink->createHardlink("link0"_filename, "target"_filename);
    for (int i = 1; i < chainLength; i++)
        tarSink->createHardlink(
            CanonPath{Filename::fromStringAsserting("link" + std::to_string(i))},
            CanonPath{Filename::fromStringAsserting("link" + std::to_string(i - 1))});

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("target"_filename, "chained content"));
    ASSERT_THAT(accessor, testing::HasContents("link0"_filename, "chained content"));
    ASSERT_THAT(
        accessor,
        testing::HasContents(
            CanonPath{Filename::fromStringAsserting("link" + std::to_string(chainLength - 1))}, "chained content"));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_directory_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("dir"_filename);

    EXPECT_THAT(
        [&]() { tarSink->createHardlink("link"_filename, "dir"_filename); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("target is a directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_root_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("foo"_filename);

    EXPECT_THAT(
        [&]() { tarSink->createHardlink("bar"_filename, CanonPath::root); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("target is a directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_to_later_path_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* The target has to be something the archive has already got to, so a
       hard link cannot form a cycle either. (libarchive only warns about
       a cyclic hard link; we reject it.) */
    EXPECT_THAT(
        [&]() { tarSink->createHardlink("foo"_filename, "later"_filename); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("does not exist")));
}

TEST_F(MerkleTarAdapterTest, hardlink_keeps_the_file_it_was_made_from)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* Replacing the target afterwards does not follow through to the link:
       the link is to the file, not to the path. `tar` behaves this way
       because it unlinks the old file rather than writing through it. */
    tarSink->createRegularFile("target"_filename, false, [](Sink & sink) { sink("old"); });
    tarSink->createHardlink("link"_filename, "target"_filename);

    {
        /* Warned about, because older versions of Nix resolved the link
           against the replacement instead. */
        testing::CaptureLogging log;
        tarSink->createRegularFile("target"_filename, false, [](Sink & sink) { sink("new"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/link'"));
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("target"_filename, "new"));
    ASSERT_THAT(accessor, testing::HasContents("link"_filename, "old"));
}

TEST_F(MerkleTarAdapterTest, hardlink_replaced_itself_keeps_the_target)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    /* The other way round from `hardlink_keeps_the_file_it_was_made_from`:
       the link's own path is replaced, and the file it was made from is
       untouched. Warned about for the same reason. */
    tarSink->createRegularFile("target"_filename, false, [](Sink & sink) { sink("original"); });
    tarSink->createHardlink("link"_filename, "target"_filename);

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile("link"_filename, false, [](Sink & sink) { sink("replacement"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as '/target'"));
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("target"_filename, "original"));
    ASSERT_THAT(accessor, testing::HasContents("link"_filename, "replacement"));
}

TEST_F(MerkleTarAdapterTest, hardlink_replaced_names_all_the_other_paths)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("a"_filename, false, [](Sink & sink) { sink("shared"); });
    tarSink->createHardlink("b"_filename, "a"_filename);
    tarSink->createHardlink("c"_filename, "a"_filename);

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile("a"_filename, false, [](Sink & sink) { sink("replacement"); });
        /* All of them, not just the one it was linked from. */
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("'/b', '/c'"));
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("a"_filename, "replacement"));
    ASSERT_THAT(accessor, testing::HasContents("b"_filename, "shared"));
    ASSERT_THAT(accessor, testing::HasContents("c"_filename, "shared"));
}

TEST_F(MerkleTarAdapterTest, hardlink_warns_only_while_paths_are_still_shared)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("a"_filename, false, [](Sink & sink) { sink("shared"); });
    tarSink->createHardlink("b"_filename, "a"_filename);

    {
        testing::CaptureLogging log;
        tarSink->createRegularFile("a"_filename, false, [](Sink & sink) { sink("first"); });
        EXPECT_THAT(log.get(), testing::HasSubstrIgnoreANSIMatcher("the same file as"));
    }

    {
        /* `b` is on its own now, so replacing it is unremarkable. */
        testing::CaptureLogging log;
        tarSink->createRegularFile("b"_filename, false, [](Sink & sink) { sink("second"); });
        EXPECT_THAT(log.get(), ::testing::Not(testing::HasSubstrIgnoreANSIMatcher("the same file as")));
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("a"_filename, "first"));
    ASSERT_THAT(accessor, testing::HasContents("b"_filename, "second"));
}

TEST_F(MerkleTarAdapterTest, hardlink_failure_says_what_it_was_doing)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("content"); });

    /* The error itself is about `foo/bar`; the trace says why we were
       creating it. */
    EXPECT_THAT(
        [&]() { tarSink->createHardlink("/foo/bar"_canonPath, "foo"_filename); },
        ::testing::ThrowsMessage<Error>(::testing::AllOf(
            testing::HasSubstrIgnoreANSIMatcher("parent of '/foo/bar' is not a directory"),
            testing::HasSubstrIgnoreANSIMatcher("while creating a hard link from '/foo/bar' to '/foo'"))));
}

TEST_F(MerkleTarAdapterTest, child_of_file_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile("/foo/bar"_canonPath, false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, symlink_child_of_file_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createSymlink("/foo/bar"_canonPath, "target"); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, hardlink_child_of_file_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createHardlink("/foo/bar"_canonPath, "foo"_filename); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, child_of_symlink_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Symlink points to a valid directory, but we still can't create children under the symlink path
    tarSink->createDirectory("target"_filename);
    tarSink->createSymlink("foo"_filename, "target");

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile("/foo/bar"_canonPath, false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, explicit_directory_replaces_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Create a file at "foo"
    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("content"); });
    // Explicitly replace it with a directory
    tarSink->createDirectory("foo"_filename);
    // Now we can create children
    tarSink->createRegularFile("/foo/bar"_canonPath, false, [](Sink & sink) { sink("child content"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("/foo/bar"_canonPath, "child content"));
}

TEST_F(MerkleTarAdapterTest, large_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Create a file larger than the buffering threshold (1 MiB)
    std::string largeContent(2 * 1024 * 1024, 'x');

    tarSink->createRegularFile("large.bin"_filename, false, [&](Sink & sink) { sink(largeContent); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("large.bin"_filename, largeContent));
}

TEST_F(MerkleTarAdapterTest, large_file_in_chunks)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Create a file larger than the buffering threshold (1 MiB)
    std::string largeContent(2 * 1024 * 1024, 'x');

    tarSink->createRegularFile("large.bin"_filename, false, [&](Sink & sink) {
        sink(largeContent);
        sink(largeContent);
    });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("large.bin"_filename, largeContent + largeContent));
}

TEST_F(MerkleTarAdapterTest, replacing_empty_directory)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("foo"_filename);
    tarSink->createDirectory("/foo/bar"_canonPath);
    /* Under tarball unpacking semantics, creating the same directories
       (implicitly or explicitly) is fine. */
    tarSink->createDirectory("/foo/bar"_canonPath);
    tarSink->createDirectory("foo"_filename);

    tarSink->createRegularFile("/foo/bar"_canonPath, false, [](Sink & sink) { sink("test"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasDirectory("foo"_filename, std::set<std::string>{"bar"}));
    ASSERT_THAT(accessor, testing::HasContents("/foo/bar"_canonPath, "test"));
}

TEST_F(MerkleTarAdapterTest, replacing_non_empty_directory_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("foo"_filename);
    tarSink->createDirectory("/foo/bar"_canonPath);

    /* This fails. libarchive (and other tarball unpackers) doesn't recursively unlink existing non-empty
       directories.
       https://github.com/libarchive/libarchive/blob/761652401fe35fca9744607a0cf0009afbf04f42/libarchive/archive_write_disk_posix.c#L3411-L3417
     */
    EXPECT_THAT(
        [&]() { tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("test"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("conflicting non-empty directory")));
}

TEST_F(MerkleTarAdapterTest, replacing_non_empty_directory_with_symlink_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory("foo"_filename);
    tarSink->createRegularFile("/foo/bar"_canonPath, false, [](Sink & sink) { sink("test"); });

    EXPECT_THAT(
        [&]() { tarSink->createSymlink("foo"_filename, "target"); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("conflicting non-empty directory")));
}

TEST_F(MerkleTarAdapterTest, child_of_hardlink_fails)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("foo"_filename, false, [](Sink & sink) { sink("test"); });
    tarSink->createHardlink("bar"_filename, "foo"_filename);

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile("/bar/baz"_canonPath, false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("not a directory")));
}

TEST_F(MerkleTarAdapterTest, many_files)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    constexpr int numFiles = 100;
    for (int i = 0; i < numFiles; i++) {
        auto path = CanonPath{Filename::fromStringAsserting("file" + std::to_string(i) + ".txt")};
        auto content = "content " + std::to_string(i);
        tarSink->createRegularFile(path, false, [&](Sink & sink) { sink(content); });
    }

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), static_cast<size_t>(numFiles));

    for (int i = 0; i < numFiles; i++) {
        auto path = CanonPath{Filename::fromStringAsserting("file" + std::to_string(i) + ".txt")};
        auto expectedContent = "content " + std::to_string(i);
        ASSERT_THAT(accessor, testing::HasContents(path, expectedContent));
    }
}

TEST_F(MerkleTarAdapterTest, deep_nesting)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(
        "/a/b/c/d/e/f/g/h/file.txt"_canonPath, false, [](Sink & sink) { sink("deeply nested"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("/a/b/c/d/e/f/g/h/file.txt"_canonPath, "deeply nested"));
}

TEST_F(MerkleTarAdapterTest, directory_with_symlink_and_empty_subdir)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("file.txt"_filename, false, [](Sink & sink) { sink("content"); });
    tarSink->createSymlink("link"_filename, "file.txt");
    tarSink->createDirectory("empty"_filename);

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("file.txt"_filename, "content"));
    ASSERT_THAT(accessor, testing::HasSymlink("link"_filename, "file.txt"));
    ASSERT_THAT(accessor, testing::HasDirectory("empty"_filename, std::set<std::string>{}));
}

// Tests for "last wins" tar semantics - when a path appears multiple times,
// the last entry should win.

TEST_F(MerkleTarAdapterTest, last_wins_file_overwrites_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("file.txt"_filename, false, [](Sink & sink) { sink("first content"); });
    tarSink->createRegularFile("file.txt"_filename, false, [](Sink & sink) { sink("second content"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("file.txt"_filename, "second content"));
}

TEST_F(MerkleTarAdapterTest, last_wins_file_overwrites_symlink)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink("entry"_filename, "some-target");
    tarSink->createRegularFile("entry"_filename, false, [](Sink & sink) { sink("file content"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("entry"_filename, "file content"));
}

TEST_F(MerkleTarAdapterTest, last_wins_symlink_overwrites_file)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("entry"_filename, false, [](Sink & sink) { sink("file content"); });
    tarSink->createSymlink("entry"_filename, "new-target");

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasSymlink("entry"_filename, "new-target"));
}

TEST_F(MerkleTarAdapterTest, last_wins_symlink_overwrites_symlink)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink("link"_filename, "first-target");
    tarSink->createSymlink("link"_filename, "second-target");

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasSymlink("link"_filename, "second-target"));
}

TEST_F(MerkleTarAdapterTest, last_wins_executable_changes)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("script"_filename, false, [](Sink & sink) { sink("content"); });
    tarSink->createRegularFile("script"_filename, true, [](Sink & sink) { sink("content"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    // The file should be executable now (mode change)
    ASSERT_THAT(accessor, testing::HasContents("script"_filename, "content"));
    ASSERT_TRUE(accessor->lstat("script"_filename).isExecutable);
}

TEST_F(MerkleTarAdapterTest, last_wins_nested_file_overwrites)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile("/a/b/file.txt"_canonPath, false, [](Sink & sink) { sink("first"); });
    tarSink->createRegularFile("/a/b/file.txt"_canonPath, false, [](Sink & sink) { sink("second"); });

    auto result = tarSink->flush();

    auto accessor = getAccessor(result.hash);
    ASSERT_THAT(accessor, testing::HasContents("/a/b/file.txt"_canonPath, "second"));
}

TEST_F(MerkleTarAdapterTest, last_wins_at_root)
{
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createSymlink(CanonPath::root, "first-target");
    tarSink->createSymlink(CanonPath::root, "second-target");

    auto result = tarSink->flush();
    ASSERT_EQ(result.mode, merkle::Mode::Symlink);

    // Wrap to verify
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link"_filename, result);
    auto dirHash = std::move(*dirSink).finalize();

    pool->flush();
    auto accessor = getAccessor(dirHash);
    ASSERT_THAT(accessor, testing::HasSymlink("link"_filename, "second-target"));
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

    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    // Track what the final content should be for each path
    std::map<std::string, std::string> expected;

    for (auto & [path, content] : entries) {
        tarSink->createRegularFile(
            CanonPath{Filename::fromStringAsserting(path)}, false, [&](Sink & sink) { sink(content); });
        expected[path] = content; // Last write wins
    }

    auto result = tarSink->flush();
    auto accessor = getAccessor(result.hash);

    // Verify each path has its expected final content
    for (auto & [path, content] : expected) {
        RC_ASSERT(accessor->readFile(CanonPath{Filename::fromStringAsserting(path)}) == content);
    }
}

} // namespace nix
