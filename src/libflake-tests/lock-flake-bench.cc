#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>

#include "nix/flake/flake.hh"
#include "nix/flake/flakeref.hh"
#include "nix/flake/settings.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/store-open.hh"
#include "nix/util/file-system.hh"
#include "nix/util/experimental-features.hh"

namespace nix {

namespace fs = std::filesystem;

static void writeFlakeFile(const fs::path & path, const std::string & contents)
{
    fs::create_directories(path.parent_path());
    writeFile(path.string(), contents);
}

/* Build a two-level flake graph in a fresh temp directory and return the path
   of the top-level flake.

   - relative == true:  top -> dep, where dep has a *relative* 'path:./sub'
     input. This is the shape exercised by the #14762 fix: on an incremental
     lock, `dep` is kept from the existing lock, and because it has a relative
     input the fix re-fetches its source to resolve that input correctly.
   - relative == false: top -> dep -> leaf, all non-relative. The fix never
     fires here; used as a control to confirm there is no regression.

   `depFiles` extra files are written into `dep` to scale the cost of
   fetching/hashing its source tree. */
static fs::path makeFlakeGraph(const fs::path & root, bool relative, int depFiles)
{
    auto top = root / "top";
    auto dep = root / "dep";

    if (relative) {
        writeFlakeFile(dep / "sub" / "flake.nix", "{ outputs = _: { }; }\n");
        writeFlakeFile(dep / "flake.nix", "{ inputs.sub.url = \"path:./sub\"; outputs = _: { }; }\n");
    } else {
        auto leaf = root / "leaf";
        writeFlakeFile(leaf / "flake.nix", "{ outputs = _: { }; }\n");
        writeFlakeFile(dep / "flake.nix", "{ inputs.leaf.url = \"path:" + leaf.string() + "\"; outputs = _: { }; }\n");
    }

    for (int i = 0; i < depFiles; ++i)
        writeFlakeFile(dep / "files" / ("f" + std::to_string(i) + ".txt"), "content-" + std::to_string(i) + "\n");

    writeFlakeFile(top / "flake.nix", "{ inputs.dep.url = \"path:" + dep.string() + "\"; outputs = _: { }; }\n");

    return top;
}

/* The store and settings, shared across all iterations of one benchmark. The
   store is created once (its initialization is heavy and noisy, so we keep it
   out of the measured loop). A *fresh* EvalState is built per iteration instead
   (see makeState), which keeps the in-memory fetch/eval caches cold so the
   "keep vs. refetch" decision is actually re-run on every measured lock -- as
   it would be on a fresh `nix` invocation. */
struct StoreEnv
{
    ref<Store> store;
    fetchers::Settings fetchSettings;
    flake::Settings flakeSettings;

    explicit StoreEnv(const fs::path & storeRoot)
        : store(openStore(storeRoot.string()))
    {
    }
};

/* EvalSettings takes a `bool &` it keeps a reference to, and EvalState keeps a
   reference to the EvalSettings, so both must outlive the returned state.
   They are owned by the state via these out-parameters. */
static std::shared_ptr<EvalState>
makeState(StoreEnv & env, std::unique_ptr<bool> & readOnlyOut, std::unique_ptr<EvalSettings> & settingsOut)
{
    readOnlyOut = std::make_unique<bool>(false);
    settingsOut = std::make_unique<EvalSettings>(*readOnlyOut);
    settingsOut->nixPath = {};
    env.flakeSettings.configureEvalSettings(*settingsOut);
    return std::make_shared<EvalState>(LookupPath{}, env.store, env.fetchSettings, *settingsOut, nullptr);
}

static void doBench(benchmark::State & state, bool relative)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);

    const int depFiles = static_cast<int>(state.range(0));

    auto root = createTempDir();
    AutoDelete cleanupRoot(root, true);
    auto topDir = makeFlakeGraph(root, relative, depFiles);

    auto storeRoot = createTempDir();
    AutoDelete cleanupStore(storeRoot, true);
    StoreEnv env{storeRoot};

    /* Establish the lock file once so the measured locks below take the
       "keep existing input" path rather than locking from scratch. */
    {
        std::unique_ptr<bool> ro;
        std::unique_ptr<EvalSettings> es;
        auto st = makeState(env, ro, es);
        auto flakeRef = parseFlakeRef(env.fetchSettings, "path:" + topDir.string());
        flake::LockFlags flags;
        flags.writeLockFile = true;
        lockFlake(env.flakeSettings, *st, flakeRef, flags);
    }

    for (auto _ : state) {
        state.PauseTiming();
        std::unique_ptr<bool> ro;
        std::unique_ptr<EvalSettings> es;
        auto st = makeState(env, ro, es);
        auto flakeRef = parseFlakeRef(env.fetchSettings, "path:" + topDir.string());
        flake::LockFlags flags;
        flags.updateLockFile = true;
        flags.writeLockFile = false; // read the existing lock; don't rewrite it
        state.ResumeTiming();

        auto locked = lockFlake(env.flakeSettings, *st, flakeRef, flags);
        benchmark::DoNotOptimize(locked.lockFile.root);
    }
}

static void BM_LockFlake_NoRelative(benchmark::State & state)
{
    doBench(state, /*relative=*/false);
}

static void BM_LockFlake_NestedRelative(benchmark::State & state)
{
    doBench(state, /*relative=*/true);
}

/* Arg(N) = number of extra files in `dep`'s source tree. */
BENCHMARK(BM_LockFlake_NoRelative)->Arg(0)->Arg(2'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_LockFlake_NestedRelative)->Arg(0)->Arg(2'000)->Unit(benchmark::kMillisecond);

} // namespace nix
