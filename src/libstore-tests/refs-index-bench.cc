#include <benchmark/benchmark.h>

#include "nix/store/local-store.hh"
#include "nix/store/store-open.hh"
#include "nix/util/file-system.hh"
#include "nix/util/hash.hh"

#ifndef _WIN32

#  include <filesystem>

using namespace nix;

enum class IndexMode { WithIndex, WithoutIndex };

/**
 * Open a separate SQLite connection to the store's DB and
 * create or drop the IndexReferrer index.  Called during
 * PauseTiming so it doesn't affect measurements.
 */
static void ensureIndexMode(LocalStore & localStore, IndexMode mode)
{
    auto dbPath = localStore.dbDir / "db.sqlite";
    SQLite db(dbPath, {.mode = SQLiteOpenMode::Normal, .useWAL = true});
    if (mode == IndexMode::WithIndex)
        db.exec("create index if not exists IndexReferrer on Refs(referrer)");
    else
        db.exec("drop index if exists IndexReferrer");
}

struct TempStore
{
    std::filesystem::path tmpRoot;
    std::shared_ptr<Store> store;
    std::shared_ptr<LocalStore> localStore;

    TempStore()
    {
        tmpRoot = createTempDir();
        std::filesystem::create_directories(tmpRoot / "nix/store");
        store = openStore(fmt("local?root=%s", tmpRoot.string()));
        localStore = std::dynamic_pointer_cast<LocalStore>(store);
        if (!localStore)
            throw Error("expected local store");
    }

    ~TempStore()
    {
        localStore.reset();
        store.reset();
        std::filesystem::remove_all(tmpRoot);
    }
};

// ---------------------------------------------------------------------------
// A) Write Path — register N paths each with one reference
// ---------------------------------------------------------------------------

static void BM_RefsWrite(benchmark::State & state, IndexMode mode)
{
    const int N = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();
        TempStore ts;
        ensureIndexMode(*ts.localStore, mode);

        // Pre-register target paths that will be referenced
        ValidPathInfos targets;
        std::vector<StorePath> targetPaths;
        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("ref-target-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            targetPaths.push_back(p);
            targets.emplace(p, std::move(info));
        }
        ts.localStore->registerValidPaths(targets);

        // Prepare referrer paths that each reference one target
        ValidPathInfos referrers;
        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("ref-source-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            info.references.insert(targetPaths[i]);
            referrers.emplace(p, std::move(info));
        }

        state.ResumeTiming();
        ts.localStore->registerValidPaths(referrers);
    }

    state.SetItemsProcessed(state.iterations() * N);
}

static void BM_RefsWrite_WithIndex(benchmark::State & state)
{
    BM_RefsWrite(state, IndexMode::WithIndex);
}
static void BM_RefsWrite_WithoutIndex(benchmark::State & state)
{
    BM_RefsWrite(state, IndexMode::WithoutIndex);
}

// ---------------------------------------------------------------------------
// B) Read Path — query references and referrers
// ---------------------------------------------------------------------------

static void BM_RefsRead(benchmark::State & state, IndexMode mode)
{
    const int N = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();
        TempStore ts;

        // Populate: N source paths each referencing one target path
        ValidPathInfos targets;
        std::vector<StorePath> targetPaths;
        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("read-target-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            targetPaths.push_back(p);
            targets.emplace(p, std::move(info));
        }
        ts.localStore->registerValidPaths(targets);

        ValidPathInfos sources;
        std::vector<StorePath> sourcePaths;
        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("read-source-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            info.references.insert(targetPaths[i]);
            sourcePaths.push_back(p);
            sources.emplace(p, std::move(info));
        }
        ts.localStore->registerValidPaths(sources);

        ensureIndexMode(*ts.localStore, mode);
        state.ResumeTiming();

        // Forward lookups (queryReferences via queryPathInfo)
        for (int i = 0; i < N; ++i) {
            auto info = ts.localStore->queryPathInfo(sourcePaths[i]);
            benchmark::DoNotOptimize(info->references);
        }

        // Reverse lookups (queryReferrers)
        for (int i = 0; i < N; ++i) {
            StorePathSet referrers;
            ts.localStore->queryReferrers(targetPaths[i], referrers);
            benchmark::DoNotOptimize(referrers);
        }
    }

    state.SetItemsProcessed(state.iterations() * N * 2);
}

static void BM_RefsRead_WithIndex(benchmark::State & state)
{
    BM_RefsRead(state, IndexMode::WithIndex);
}
static void BM_RefsRead_WithoutIndex(benchmark::State & state)
{
    BM_RefsRead(state, IndexMode::WithoutIndex);
}

// ---------------------------------------------------------------------------
// C) Mixed Read/Write — 10 writes per 2 reads
// ---------------------------------------------------------------------------

static void BM_RefsMixed(benchmark::State & state, IndexMode mode)
{
    const int N = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();
        TempStore ts;

        // Pre-populate base targets
        ValidPathInfos baseTargets;
        std::vector<StorePath> baseTargetPaths;
        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("mixed-base-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            baseTargetPaths.push_back(p);
            baseTargets.emplace(p, std::move(info));
        }
        ts.localStore->registerValidPaths(baseTargets);

        ensureIndexMode(*ts.localStore, mode);

        // Prepare N paths to write
        ValidPathInfos currentBatch;

        for (int i = 0; i < N; ++i) {
            auto p = StorePath::random(fmt("mixed-write-%d", i));
            ValidPathInfo info{p, UnkeyedValidPathInfo(*ts.localStore, Hash::dummy)};
            info.narSize = 1;
            info.references.insert(baseTargetPaths[i % baseTargetPaths.size()]);
            currentBatch.emplace(p, std::move(info));
        }

        state.ResumeTiming();

        // Interleave: register 10 paths, then do 2 reads
        auto it = currentBatch.begin();
        int ops = 0;
        while (it != currentBatch.end()) {
            // Write batch of up to 10
            ValidPathInfos batch;
            for (int w = 0; w < 10 && it != currentBatch.end(); ++w, ++it) {
                batch.emplace(it->first, std::move(it->second));
            }
            ts.localStore->registerValidPaths(batch);
            ops += batch.size();

            // 2 reads
            for (int r = 0; r < 2; ++r) {
                auto & target = baseTargetPaths[(ops + r) % baseTargetPaths.size()];
                StorePathSet referrers;
                ts.localStore->queryReferrers(target, referrers);
                benchmark::DoNotOptimize(referrers);
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * N);
}

static void BM_RefsMixed_WithIndex(benchmark::State & state)
{
    BM_RefsMixed(state, IndexMode::WithIndex);
}
static void BM_RefsMixed_WithoutIndex(benchmark::State & state)
{
    BM_RefsMixed(state, IndexMode::WithoutIndex);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

BENCHMARK(BM_RefsWrite_WithIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);
BENCHMARK(BM_RefsWrite_WithoutIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);
BENCHMARK(BM_RefsRead_WithIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);
BENCHMARK(BM_RefsRead_WithoutIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);
BENCHMARK(BM_RefsMixed_WithIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);
BENCHMARK(BM_RefsMixed_WithoutIndex)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000)->Arg(100000);

#endif
