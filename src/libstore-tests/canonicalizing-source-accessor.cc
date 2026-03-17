#include <gtest/gtest.h>
#include "nix/store/canonicalizing-source-accessor.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/archive.hh"
#include "nix/util/hash.hh"
#include "nix/util/posix-source-accessor.hh"

#ifndef _WIN32

#  include <fstream>
#  include <sys/stat.h>

using namespace nix;

static const StringSet emptyAcls{};

#  define TEST_CANON_OPTIONS {.uidRange = std::nullopt, NIX_WHEN_SUPPORT_ACLS(emptyAcls)}

class CanonicalizingSourceAccessorTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpDir;
    AutoDelete tmpDirCleanup;

    void SetUp() override
    {
        tmpDir = createTempDir();
        tmpDirCleanup = AutoDelete(tmpDir, true);
    }

    void createFile(const std::filesystem::path & path, const std::string & content, mode_t mode = 0644)
    {
        std::ofstream out(path, std::ios::binary);
        out << content;
        out.close();
        ::chmod(path.c_str(), mode);
    }

    void createDir(const std::filesystem::path & path, mode_t mode = 0755)
    {
        std::filesystem::create_directories(path);
        ::chmod(path.c_str(), mode);
    }

    void createSymlink(const std::filesystem::path & target, const std::filesystem::path & link)
    {
        std::filesystem::create_symlink(target, link);
    }
};

TEST_F(CanonicalizingSourceAccessorTest, SetsCanonicalPermissions)
{
    auto filePath = tmpDir / "testfile";
    createFile(filePath, "hello", 0666);

    auto st = lstat(filePath);
    InodesSeen inodesSeen;
    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    canonicaliseOneFile(filePath, st, options, inodesSeen);

    auto st2 = lstat(filePath);
    mode_t mode = st2.st_mode & ~S_IFMT;
    EXPECT_EQ(mode, 0444u);
    EXPECT_EQ(st2.st_mtime, 1);
}

TEST_F(CanonicalizingSourceAccessorTest, PreservesExecutableBit)
{
    auto filePath = tmpDir / "execfile";
    createFile(filePath, "#!/bin/sh\necho hello", 0755);

    auto st = lstat(filePath);
    InodesSeen inodesSeen;
    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    canonicaliseOneFile(filePath, st, options, inodesSeen);

    auto st2 = lstat(filePath);
    mode_t mode = st2.st_mode & ~S_IFMT;
    EXPECT_EQ(mode, 0555u);
}

TEST_F(CanonicalizingSourceAccessorTest, CanonicalizesDirPermissions)
{
    auto dirPath = tmpDir / "subdir";
    createDir(dirPath, 0700);

    auto st = lstat(dirPath);
    InodesSeen inodesSeen;
    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    canonicaliseOneFile(dirPath, st, options, inodesSeen);

    auto st2 = lstat(dirPath);
    mode_t mode = st2.st_mode & ~S_IFMT;
    EXPECT_EQ(mode, 0555u);
}

TEST_F(CanonicalizingSourceAccessorTest, ProducesCorrectNAR)
{
    auto root = tmpDir / "nartest";
    createDir(root, 0700);
    createFile(root / "regular.txt", "some content", 0666);
    createFile(root / "executable.sh", "#!/bin/sh", 0755);
    createDir(root / "subdir", 0700);
    createFile(root / "subdir" / "nested.txt", "nested content", 0644);
    createSymlink("../regular.txt", root / "subdir" / "link");

    InodesSeen inodesSeen;
    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    auto sourcePath = PosixSourceAccessor::createAtRoot(root);
    auto canonAccessor = make_ref<CanonicalizingSourceAccessor>(sourcePath.accessor, options, inodesSeen);

    HashSink hashSink1{HashAlgorithm::SHA256};
    SourcePath canonPath{canonAccessor, sourcePath.path};
    canonPath.dumpPath(hashSink1);
    auto hash1 = hashSink1.finish();

    // Verify files on disk are now canonical
    auto st = lstat(root / "regular.txt");
    EXPECT_EQ(st.st_mode & ~S_IFMT, 0444u);
    EXPECT_EQ(st.st_mtime, 1);

    auto stExec = lstat(root / "executable.sh");
    EXPECT_EQ(stExec.st_mode & ~S_IFMT, 0555u);

    // Dump the same (already-canonical) tree to verify NAR hash matches
    auto sourcePath2 = PosixSourceAccessor::createAtRoot(root);
    HashSink hashSink2{HashAlgorithm::SHA256};
    sourcePath2.dumpPath(hashSink2);
    auto hash2 = hashSink2.finish();

    EXPECT_EQ(hash1.hash, hash2.hash);
    EXPECT_EQ(hash1.numBytesDigested, hash2.numBytesDigested);
}

TEST_F(CanonicalizingSourceAccessorTest, MatchesTwoPassResult)
{
    auto root1 = tmpDir / "twopass";
    createDir(root1, 0700);
    createFile(root1 / "a.txt", "file a content", 0666);
    createFile(root1 / "b.sh", "#!/bin/sh\necho b", 0755);
    createDir(root1 / "sub", 0700);
    createFile(root1 / "sub" / "c.txt", "sub c content", 0644);

    auto root2 = tmpDir / "singlepass";
    std::filesystem::copy(root1, root2, std::filesystem::copy_options::recursive);

    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    // Two-pass approach: canonicalize then dump
    {
        InodesSeen inodesSeen;
        canonicalisePathMetaData(root1, options, inodesSeen);
    }
    auto sp1 = PosixSourceAccessor::createAtRoot(root1);
    HashSink hashSink1{HashAlgorithm::SHA256};
    sp1.dumpPath(hashSink1);
    auto twoPassHash = hashSink1.finish();

    // Single-pass approach: CanonicalizingSourceAccessor
    InodesSeen inodesSeen2;
    auto sp2 = PosixSourceAccessor::createAtRoot(root2);
    auto canonAccessor = make_ref<CanonicalizingSourceAccessor>(sp2.accessor, options, inodesSeen2);
    HashSink hashSink2{HashAlgorithm::SHA256};
    SourcePath canonSP{canonAccessor, sp2.path};
    canonSP.dumpPath(hashSink2);
    auto singlePassHash = hashSink2.finish();

    EXPECT_EQ(twoPassHash.hash, singlePassHash.hash);
    EXPECT_EQ(twoPassHash.numBytesDigested, singlePassHash.numBytesDigested);
}

TEST_F(CanonicalizingSourceAccessorTest, ExecutableFlagCorrect)
{
    auto root = tmpDir / "execflag";
    createDir(root, 0700);
    createFile(root / "noexec.txt", "regular file", 0644);
    createFile(root / "exec.sh", "#!/bin/sh", 0755);
    createFile(root / "partial.py", "#!/usr/bin/env python3", 0744);

    InodesSeen inodesSeen;
    CanonicalizePathMetadataOptions options TEST_CANON_OPTIONS;

    auto sp = PosixSourceAccessor::createAtRoot(root);
    auto canonAccessor = make_ref<CanonicalizingSourceAccessor>(sp.accessor, options, inodesSeen);

    auto stNoExec = canonAccessor->lstat(sp.path / "noexec.txt");
    EXPECT_FALSE(stNoExec.isExecutable);

    auto stExec = canonAccessor->lstat(sp.path / "exec.sh");
    EXPECT_TRUE(stExec.isExecutable);

    // 0744 has owner execute bit set
    auto stPartial = canonAccessor->lstat(sp.path / "partial.py");
    EXPECT_TRUE(stPartial.isExecutable);
}

#endif
