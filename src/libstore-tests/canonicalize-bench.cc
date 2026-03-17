#include <benchmark/benchmark.h>

#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/file-system.hh"

#ifndef _WIN32

#  include <filesystem>
#  include <fstream>
#  include <sys/stat.h>

using namespace nix;

#  if NIX_SUPPORT_ACL
static const StringSet emptyAcls{};
#  endif

/**
 * Create a directory tree with the given number of files.
 * If `canonical` is true, directories get 0555 and files 0444 (matching
 * what canonicalisePathMetaData sets). Otherwise, directories get 0755
 * and files 0644 (typical defaults from mkdir/open).
 *
 * Permissions are applied after all content is created so that
 * read-only directories don't block file creation.
 */
static void createTree(const std::filesystem::path & root, int fileCount, bool canonical)
{
    std::filesystem::create_directories(root);

    /* Create a few subdirectories to make the tree realistic. */
    int dirsCreated = 0;
    int filesPerDir = std::max(1, fileCount / 10);
    std::vector<std::filesystem::path> dirs;
    dirs.push_back(root);

    for (int i = 0; i < fileCount; ++i) {
        if (i % filesPerDir == 0) {
            auto subdir = root / ("dir" + std::to_string(dirsCreated++));
            std::filesystem::create_directory(subdir);
            dirs.push_back(subdir);
        }

        auto dir = root / ("dir" + std::to_string(i / filesPerDir));
        auto file = dir / ("file" + std::to_string(i));
        {
            std::ofstream ofs(file, std::ios::binary);
            ofs << "content-" << i;
        }
        if (canonical)
            chmod(file.c_str(), 0444);
    }

    /* Set directory permissions last so 0555 doesn't block file creation. */
    if (canonical)
        for (auto & d : dirs)
            chmod(d.c_str(), 0555);
}

/**
 * Make a tree writable again so remove_all can delete it.
 */
static void makeWritable(const std::filesystem::path & root)
{
    for (auto & entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_directory())
            chmod(entry.path().c_str(), 0755);
    chmod(root.c_str(), 0755);
}

/**
 * Benchmark: canonicalize an already-canonical tree.
 * This measures the cost of the redundant pass being eliminated.
 */
static void BM_CanonicalizeAlreadyCanonical(benchmark::State & state)
{
    const int fileCount = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();
        auto tmpDir = createTempDir();
        auto tree = tmpDir / "store-output";
        createTree(tree, fileCount, /*canonical=*/true);
        InodesSeen inodesSeen;
        state.ResumeTiming();

        canonicalisePathMetaData(tree, {.uidRange = std::nullopt, NIX_WHEN_SUPPORT_ACLS(emptyAcls)}, inodesSeen);

        state.PauseTiming();
        makeWritable(tmpDir);
        std::filesystem::remove_all(tmpDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * fileCount);
}

/**
 * Benchmark: canonicalize a fresh tree with non-canonical permissions.
 * This measures the cost of the necessary first canonicalize pass.
 */
static void BM_CanonicalizeFreshTree(benchmark::State & state)
{
    const int fileCount = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();
        auto tmpDir = createTempDir();
        auto tree = tmpDir / "store-output";
        createTree(tree, fileCount, /*canonical=*/false);
        InodesSeen inodesSeen;
        state.ResumeTiming();

        canonicalisePathMetaData(tree, {.uidRange = std::nullopt, NIX_WHEN_SUPPORT_ACLS(emptyAcls)}, inodesSeen);

        state.PauseTiming();
        makeWritable(tmpDir);
        std::filesystem::remove_all(tmpDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * fileCount);
}

BENCHMARK(BM_CanonicalizeAlreadyCanonical)->Arg(100)->Arg(1000)->Arg(5000);
BENCHMARK(BM_CanonicalizeFreshTree)->Arg(100)->Arg(1000)->Arg(5000);

#endif
