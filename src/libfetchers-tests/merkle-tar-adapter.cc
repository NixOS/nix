#include "nix/fetchers/git-utils.hh"
#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/file-system.hh"
#include "nix/util/serialise.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <git2/global.h>
#include <git2/repository.h>

namespace nix {

class MerkleTarAdapterTest : public ::testing::Test
{
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);

        git_libgit2_init();
        git_repository * repo = nullptr;
        auto r = git_repository_init(&repo, tmpDir.string().c_str(), 0);
        ASSERT_EQ(r, 0);
        git_repository_free(repo);
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }

    ref<GitRepo> openRepo()
    {
        return GitRepo::openRepo(tmpDir, {.create = true});
    }

    ref<GitRepoPool> openWriterPool()
    {
        return GitRepoPool::create(tmpDir, {.create = true});
    }

    std::string getRepoName() const
    {
        return tmpDir.filename().string();
    }
};

TEST_F(MerkleTarAdapterTest, empty_archive_throws)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    EXPECT_THROW(tarSink->flush(), Error);
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
    pool->flushAndSetAllowDependentCreation(true);
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("file", result);
    auto dirHash = std::move(*dirSink).flush();

    pool->flushAndSetAllowDependentCreation(false);
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_EQ(accessor->readFile(CanonPath("file")), "hello world");
}

TEST_F(MerkleTarAdapterTest, single_executable_file)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("script.sh"), true, [](Sink & sink) { sink("#!/bin/bash\necho hello"); });

    auto result = tarSink->flush();

    pool->flushAndSetAllowDependentCreation(false);
    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_EQ(accessor->readFile(CanonPath("script.sh")), "#!/bin/bash\necho hello");
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
    pool->flushAndSetAllowDependentCreation(true);
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link", result);
    auto dirHash = std::move(*dirSink).flush();

    pool->flushAndSetAllowDependentCreation(false);
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_EQ(accessor->readLink(CanonPath("link")), "target");
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
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), 0u);
}

TEST_F(MerkleTarAdapterTest, nested_empty_directories)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("a/b/c"));

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_EQ(accessor->readDirectory(CanonPath("a")).size(), 1u);
    ASSERT_EQ(accessor->readDirectory(CanonPath("a/b")).size(), 1u);
    ASSERT_EQ(accessor->readDirectory(CanonPath("a/b/c")).size(), 0u);
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
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), 2u);
    ASSERT_EQ(accessor->readFile(CanonPath("hello.txt")), "hello world");
    ASSERT_EQ(accessor->readFile(CanonPath("bye.txt")), "goodbye");
}

TEST_F(MerkleTarAdapterTest, nested_directory_with_files)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("a/b/file.txt"), false, [](Sink & sink) { sink("nested content"); });

    auto result = tarSink->flush();

    auto accessor = repo->getAccessor(result.hash, {}, getRepoName());
    ASSERT_EQ(accessor->readFile(CanonPath("a/b/file.txt")), "nested content");
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
    auto entries = accessor->readDirectory(CanonPath::root);
    ASSERT_EQ(entries.size(), 5u);
    ASSERT_EQ(accessor->readFile(CanonPath("file.txt")), "regular file");
    ASSERT_EQ(accessor->readFile(CanonPath("script.sh")), "#!/bin/bash");
    ASSERT_EQ(accessor->readLink(CanonPath("link")), "file.txt");
    ASSERT_EQ(accessor->readDirectory(CanonPath("empty")).size(), 0u);
    ASSERT_EQ(accessor->readFile(CanonPath("subdir/nested.txt")), "nested");
}

TEST_F(MerkleTarAdapterTest, hardlink_target_not_found)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createHardlink(CanonPath("link"), CanonPath("nonexistent"));

    EXPECT_THAT([&]() { tarSink->flush(); }, ::testing::ThrowsMessage<Error>(::testing::HasSubstr("not found")));
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
    ASSERT_EQ(accessor->readFile(CanonPath("original.txt")), "shared content");
    ASSERT_EQ(accessor->readFile(CanonPath("link.txt")), "shared content");
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
    ASSERT_EQ(accessor->readFile(CanonPath("script.sh")), "#!/bin/bash");
    ASSERT_EQ(accessor->readFile(CanonPath("script-link.sh")), "#!/bin/bash");
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
    ASSERT_EQ(accessor->readFile(CanonPath("original")), "content");
    ASSERT_EQ(accessor->readFile(CanonPath("link1")), "content");
    ASSERT_EQ(accessor->readFile(CanonPath("link2")), "content");
    ASSERT_EQ(accessor->readFile(CanonPath("link3")), "content");
}

TEST_F(MerkleTarAdapterTest, hardlink_to_directory_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createDirectory(CanonPath("dir"));
    tarSink->createHardlink(CanonPath("link"), CanonPath("dir"));

    EXPECT_THAT([&]() { tarSink->flush(); }, ::testing::ThrowsMessage<Error>(::testing::HasSubstr("directory")));
}

TEST_F(MerkleTarAdapterTest, child_of_file_fails)
{
    auto repo = openRepo();
    auto pool = openWriterPool();
    auto tarSink = merkle::makeTarSink(*pool);

    tarSink->createRegularFile(CanonPath("foo"), false, [](Sink & sink) { sink("content"); });

    EXPECT_THAT(
        [&]() { tarSink->createRegularFile(CanonPath("foo/bar"), false, [](Sink & sink) { sink("x"); }); },
        ::testing::ThrowsMessage<Error>(::testing::HasSubstr("not a directory")));
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
        ::testing::ThrowsMessage<Error>(::testing::HasSubstr("not a directory")));
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
    ASSERT_EQ(accessor->readFile(CanonPath("foo/bar")), "child content");
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
    ASSERT_EQ(accessor->readFile(CanonPath("large.bin")), largeContent);
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
        ASSERT_EQ(accessor->readFile(path), expectedContent);
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
    ASSERT_EQ(accessor->readFile(CanonPath("a/b/c/d/e/f/g/h/file.txt")), "deeply nested");
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
    ASSERT_EQ(accessor->readFile(CanonPath("file.txt")), "content");
    ASSERT_EQ(accessor->readLink(CanonPath("link")), "file.txt");
    ASSERT_EQ(accessor->readDirectory(CanonPath("empty")).size(), 0u);
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
    ASSERT_EQ(accessor->readFile(CanonPath("file.txt")), "second content");
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
    ASSERT_EQ(accessor->readFile(CanonPath("entry")), "file content");
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
    ASSERT_EQ(accessor->readLink(CanonPath("entry")), "new-target");
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
    ASSERT_EQ(accessor->readLink(CanonPath("link")), "second-target");
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
    ASSERT_EQ(accessor->readFile(CanonPath("script")), "content");
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
    ASSERT_EQ(accessor->readFile(CanonPath("a/b/file.txt")), "second");
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
    pool->flushAndSetAllowDependentCreation(true);
    auto dirSink = pool->makeDirectorySink();
    dirSink->insertChild("link", result);
    auto dirHash = std::move(*dirSink).flush();

    pool->flushAndSetAllowDependentCreation(false);
    auto accessor = repo->getAccessor(dirHash, {}, getRepoName());
    ASSERT_EQ(accessor->readLink(CanonPath("link")), "second-target");
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
