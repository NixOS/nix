#include <benchmark/benchmark.h>

#include "nix/store/local-store.hh"
#include "nix/store/store-open.hh"
#include "nix/store/derivations.hh"
#include "nix/util/file-system.hh"
#include "nix/util/hash.hh"

#ifndef _WIN32

#  include <filesystem>
#  include <fstream>

using namespace nix;

/**
 * Helper to create a temporary local store.
 */
struct TempLocalStore
{
    std::filesystem::path tmpRoot;
    std::filesystem::path realStoreDir;
    std::shared_ptr<Store> store;
    std::shared_ptr<LocalStore> localStore;

    TempLocalStore()
    {
        tmpRoot = createTempDir();
        realStoreDir = tmpRoot / "nix/store";
        std::filesystem::create_directories(realStoreDir);
        store = openStore(fmt("local?root=%s", tmpRoot.string()));
        localStore = std::dynamic_pointer_cast<LocalStore>(store);
        if (!localStore)
            throw Error("expected local store");
    }

    ~TempLocalStore()
    {
        localStore.reset();
        store.reset();
        std::filesystem::remove_all(tmpRoot);
    }
};

/**
 * Benchmark: register N paths then query each back via queryPathInfo.
 *
 * This measures the full write+read round-trip through the DB layer,
 * which is the code path affected by the store-path serialization change.
 */
static void BM_RegisterAndQueryPaths(benchmark::State & state)
{
    const int pathCount = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();

        TempLocalStore tempStore;
        auto & localStore = *tempStore.localStore;

        // Prepare path infos
        std::vector<StorePath> paths;
        ValidPathInfos infos;
        for (int i = 0; i < pathCount; ++i) {
            auto path = StorePath::random(fmt("bench-path-%d", i));
            ValidPathInfo info{path, UnkeyedValidPathInfo(localStore, Hash::dummy)};
            info.narSize = 42;
            paths.push_back(path);
            infos.emplace(path, std::move(info));
        }

        state.ResumeTiming();

        // Register all paths
        localStore.registerValidPaths(infos);

        // Query each path back
        for (auto & path : paths) {
            auto result = localStore.queryPathInfo(path);
            benchmark::DoNotOptimize(result);
        }
    }

    state.SetItemsProcessed(state.iterations() * pathCount);
}

BENCHMARK(BM_RegisterAndQueryPaths)->Arg(100)->Arg(1000);

/**
 * Benchmark: queryPathFromHashPart on an existing path.
 *
 * This exercises the hash-part range scan which was simplified
 * (no longer prepends storeDir prefix to the search key).
 */
static void BM_QueryPathFromHashPart(benchmark::State & state)
{
    const int pathCount = state.range(0);

    TempLocalStore tempStore;
    auto & localStore = *tempStore.localStore;

    // Pre-populate the store with paths
    std::vector<StorePath> paths;
    ValidPathInfos infos;
    for (int i = 0; i < pathCount; ++i) {
        auto path = StorePath::random(fmt("bench-hash-lookup-%d", i));
        ValidPathInfo info{path, UnkeyedValidPathInfo(localStore, Hash::dummy)};
        info.narSize = 42;
        paths.push_back(path);
        infos.emplace(path, std::move(info));
    }
    localStore.registerValidPaths(infos);

    // Benchmark: look up each path by hash part
    size_t idx = 0;
    for (auto _ : state) {
        auto & path = paths[idx % paths.size()];
        auto hashPart = path.hashPart();
        auto result = localStore.queryPathFromHashPart(std::string(hashPart));
        benchmark::DoNotOptimize(result);
        idx++;
    }
}

BENCHMARK(BM_QueryPathFromHashPart)->Arg(100)->Arg(1000);

/**
 * Benchmark: queryAllValidPaths with various store sizes.
 *
 * Measures read-back of all paths, which now reads from the pathName
 * column and uses StorePath() constructor directly.
 */
static void BM_QueryAllValidPaths(benchmark::State & state)
{
    const int pathCount = state.range(0);

    TempLocalStore tempStore;
    auto & localStore = *tempStore.localStore;

    // Pre-populate
    ValidPathInfos infos;
    for (int i = 0; i < pathCount; ++i) {
        auto path = StorePath::random(fmt("bench-all-%d", i));
        ValidPathInfo info{path, UnkeyedValidPathInfo(localStore, Hash::dummy)};
        info.narSize = 42;
        infos.emplace(path, std::move(info));
    }
    localStore.registerValidPaths(infos);

    for (auto _ : state) {
        auto result = localStore.queryAllValidPaths();
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations() * pathCount);
}

BENCHMARK(BM_QueryAllValidPaths)->Arg(100)->Arg(1000);

/**
 * Benchmark: queryReferrers on a path with many referrers.
 */
static void BM_QueryReferrers(benchmark::State & state)
{
    const int referrerCount = state.range(0);

    TempLocalStore tempStore;
    auto & localStore = *tempStore.localStore;

    // Create target path
    auto targetPath = StorePath::random("bench-referrers-target");
    ValidPathInfo targetInfo{targetPath, UnkeyedValidPathInfo(localStore, Hash::dummy)};
    targetInfo.narSize = 42;

    // Create referrers that reference the target
    ValidPathInfos infos;
    infos.emplace(targetPath, targetInfo);
    for (int i = 0; i < referrerCount; ++i) {
        auto refPath = StorePath::random(fmt("bench-referrer-%d", i));
        ValidPathInfo refInfo{refPath, UnkeyedValidPathInfo(localStore, Hash::dummy)};
        refInfo.narSize = 42;
        refInfo.references = {targetPath};
        infos.emplace(refPath, std::move(refInfo));
    }
    localStore.registerValidPaths(infos);

    for (auto _ : state) {
        StorePathSet referrers;
        localStore.queryReferrers(targetPath, referrers);
        benchmark::DoNotOptimize(referrers);
    }
}

BENCHMARK(BM_QueryReferrers)->Arg(10)->Arg(100);

#endif // !_WIN32
