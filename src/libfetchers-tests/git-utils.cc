#include "nix/fetchers/git-utils.hh"
#include "nix/util/merkle-files.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include "nix/util/serialise.hh"
#include "nix/util/git.hh"

#include <span>
#include <tuple>
#include <array>

#include <git2/global.h>
#include <git2/repository.h>
#include <git2/signature.h>
#include <git2/types.h>
#include <git2/object.h>
#include <git2/tag.h>
#include <git2/blob.h>
#include <git2/tree.h>
#include <git2/version.h>

#include <zlib.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nix::fetchers {

class GitUtilsTest : public ::testing::Test
{
    AutoDelete delTmpDir;

protected:
    std::filesystem::path tmpDir;

public:
    void SetUp() override
    {
        tmpDir = createTempDir() / "test-git-repo";
        GitRepo::openRepo(tmpDir, {.create = true, .bare = true});
        delTmpDir = AutoDelete(tmpDir, true);
    }

    void TearDown() override
    {
        delTmpDir.deletePath();
    }

    ref<GitRepo> openRepo()
    {
        return GitRepo::openRepo(tmpDir, {.create = false, .bare = true});
    }

    ref<GitRepoPool> openWriterPool()
    {
        return GitRepoPool::create(tmpDir, {.create = true, .bare = true});
    }

    std::string getRepoName() const
    {
        return tmpDir.filename().string();
    }
};

merkle::TreeEntry writeString(merkle::FileSinkBuilder & store, std::string contents, bool executable = false)
{
    auto sink = store.makeRegularFileSink();
    (*sink)(contents);
    return merkle::TreeEntry{
        executable ? merkle::Mode::Executable : merkle::Mode::Regular,
        std::move(*sink).finalize(),
    };
}

TEST_F(GitUtilsTest, sink_basic)
{
    auto repo = openRepo();
    auto pool = openWriterPool();

    // Build tree bottom-up using insertChild
    // hello file
    auto hello = writeString(*pool, "hello world");

    // bye file
    auto bye = writeString(*pool, "thanks for all the fish");

    // bye-link symlink
    auto byeLink = pool->makeSymlink("bye");

    // empty directory
    auto empty = [&] {
        auto emptyDir = pool->makeDirectorySink();
        return merkle::TreeEntry{merkle::Mode::Directory, std::move(*emptyDir).finalize()};
    }();

    // links/foo file
    auto linksFoo = writeString(*pool, "hello world");

    // links directory
    auto links = [&] {
        auto linksDir = pool->makeDirectorySink();
        linksDir->insertChild("foo", linksFoo);
        return merkle::TreeEntry{merkle::Mode::Directory, std::move(*linksDir).finalize()};
    }();

    // foo-1.1 directory (contains hello, bye, bye-link, empty, links)
    auto foo = [&] {
        auto fooDir = pool->makeDirectorySink();
        fooDir->insertChild("hello", hello);
        fooDir->insertChild("bye", bye);
        fooDir->insertChild("bye-link", byeLink);
        fooDir->insertChild("empty", empty);
        fooDir->insertChild("links", links);
        return merkle::TreeEntry{merkle::Mode::Directory, std::move(*fooDir).finalize()};
    }();

    // root directory (contains foo-1.1)
    auto rootHash = [&] {
        auto rootDir = pool->makeDirectorySink();
        rootDir->insertChild("foo-1.1", foo);
        return std::move(*rootDir).finalize();
    }();

    pool->flush();

    auto result = repo->dereferenceSingletonDirectory(rootHash);
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

TEST_F(GitUtilsTest, duplicateTreeEntries)
{
    StringSink packSink;

    auto serialiseBigEndian = [](auto x, Sink & sink) {
        std::array<unsigned char, sizeof(x)> bytes;
        writeBigEndian(bytes.data(), x);
        sink(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    };

    auto writeObjectPackHeader = [&packSink](int type, std::size_t size) {
        std::vector<char> bytes;
        unsigned char byte = type << 4 | (size & 0x0f);
        size >>= 4;

        while (size) {
            byte |= 0x80;
            bytes.push_back(byte);
            byte = size & 0x7f;
            size >>= 7;
        }

        bytes.push_back(byte);
        packSink(std::string_view(bytes.data(), bytes.size()));
    };

    /* We don't have a deflate compression sink and seems excessive to write it just for tests.
       Maybe it the future we might want to write pack files manually to work in constant memory? */
    auto compressObject = [](std::string_view data) -> std::string {
        z_stream stream = {};
        if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
            unreachable();
        Finally cleanup([&stream] { ::deflateEnd(&stream); });
        stream.next_in = reinterpret_cast<unsigned char *>(const_cast<char *>(data.data()));
        stream.avail_in = data.size();
        auto maxOutputSize = ::deflateBound(&stream, data.size());
        std::vector<unsigned char> result(maxOutputSize, '\0');
        stream.next_out = result.data();
        stream.avail_out = result.size();
        if (::deflate(&stream, Z_FINISH) != Z_STREAM_END)
            unreachable();
        result.resize(result.size() - stream.avail_out);
        return std::string(result.begin(), result.end());
    };

    /* https://git-scm.com/docs/pack-format */
    packSink("PACK");
    serialiseBigEndian(/*version=*/2u, packSink);
    serialiseBigEndian(/*numObjects=*/4u, packSink);

    auto writeBlobToPackfile = [&](std::string_view data) -> Hash {
        HashSink hashSink(HashAlgorithm::SHA1);
        git::dumpBlobPrefix(data.size(), hashSink);
        hashSink(data);
        auto hash = hashSink.finish();
        writeObjectPackHeader(/*OBJ_BLOB=*/3, data.size());
        packSink(compressObject(data));
        return hash.hash;
    };

    auto writeTreeToPackfile = [&](std::span<const std::tuple<std::string, git::Mode, Hash>> entries) -> Hash {
        using namespace std::string_literals;
        std::string tree;

        for (const auto & [name, mode, hash] : entries) {
            tree += fmt("%o %s\0"s, std::to_underlying(mode), name);
            tree += std::string_view(reinterpret_cast<const char *>(hash.hash), hash.hashSize);
        }

        writeObjectPackHeader(/*OBJ_TREE=*/2, tree.size());
        packSink(compressObject(tree));
        HashSink treeHashSink(HashAlgorithm::SHA1);
        treeHashSink(fmt("tree %d\0"s, tree.size()));
        treeHashSink(tree);
        return treeHashSink.finish().hash;
    };

    auto blob1 = writeBlobToPackfile("hi");
    ASSERT_EQ(blob1.gitRev(), "32f95c0d1244a78b2be1bab8de17906fabb2c4a8");
    auto blob2 = writeBlobToPackfile("there");
    ASSERT_EQ(blob2.gitRev(), "c78ee1a5bdf46d22da300b68d50bc45c587c3293");
    auto tree1 = writeTreeToPackfile(
        std::to_array<std::tuple<std::string, git::Mode, Hash>>({
            {"a.txt", git::Mode::Regular, blob1},
            {"a.txt", git::Mode::Executable, blob2},
        }));
    ASSERT_EQ(tree1.gitRev(), "164e45a3d3f73578e4d4f83651b5d0edc4564ba0");
    auto tree2 = writeTreeToPackfile(
        std::to_array<std::tuple<std::string, git::Mode, Hash>>({
            /* Fake blob that actually points to a tree. */
            {"a.txt", git::Mode::Regular, tree1},
        }));

    auto packHash = hashString(HashAlgorithm::SHA1, packSink.s);
    packSink(std::string_view(reinterpret_cast<const char *>(packHash.hash), packHash.hashSize));

    /* Now that the pack file is built, index it.
       TODO: If libgit2 ever hardens itself against such pack files we might need to
       rework the test and write the pack file manually. */
    ::git_indexer * indexer = nullptr;
#if LIBGIT2_VERSION_CHECK(2, 0, 0)
    ::git_indexer_options indexerOpts = GIT_INDEXER_OPTIONS_INIT;
    indexerOpts.oid_type = GIT_OID_SHA1;
#endif

    Finally freeIndexer{[&indexer] { ::git_indexer_free(indexer); }};
    ASSERT_EQ(
        ::git_indexer_new(
            &indexer,
            (tmpDir / "objects" / "pack").string().c_str(),
#if LIBGIT2_VERSION_CHECK(2, 0, 0)
            &indexerOpts
#else
            0,
            nullptr,
            nullptr
#endif
            ),
        0);

    ::git_indexer_progress progress = {};
    ASSERT_EQ(::git_indexer_append(indexer, packSink.s.data(), packSink.s.size(), &progress), 0);
    ASSERT_EQ(::git_indexer_commit(indexer, &progress), 0);

    auto repo = openRepo();
    auto accessor1 = repo->getAccessor(tree1, {}, getRepoName());
    ASSERT_THAT(
        [&] { accessor1->readDirectory(CanonPath::root); },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("duplicate tree entry 'test-git-repo/a.txt'")));
    ASSERT_THAT(
        [&] { accessor1->readFile(CanonPath("a.txt")); },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("duplicate tree entry 'test-git-repo/a.txt'")));

    auto accessor2 = repo->getAccessor(tree2, {}, getRepoName());
    ASSERT_THAT(
        [&] { accessor2->readFile(CanonPath("a.txt")); },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("'test-git-repo/a.txt' is not a regular file")));
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
