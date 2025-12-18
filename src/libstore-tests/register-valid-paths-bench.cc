#include <benchmark/benchmark.h>

#include "nix/store/derivations.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-open.hh"
#include "nix/util/file-system.hh"
#include "nix/util/hash.hh"
#include "nix/util/tests/test-data.hh"

#ifndef _WIN32

#  include <filesystem>
#  include <fstream>

using namespace nix;

static void BM_RegisterValidPathsDerivations(benchmark::State & state)
{
    const int derivationCount = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();

        auto tmpRoot = createTempDir();
        auto realStoreDir = tmpRoot / "nix/store";
        std::filesystem::create_directories(realStoreDir);

        std::shared_ptr<Store> store = openStore(fmt("local?root=%s", tmpRoot.string()));
        auto localStore = std::dynamic_pointer_cast<LocalStore>(store);
        if (!localStore)
            throw Error("expected local store");

        ValidPathInfos infos;
        for (int i = 0; i < derivationCount; ++i) {
            std::string drvName = fmt("register-valid-paths-bench-%d", i);
            auto drvPath = StorePath::random(drvName + ".drv");

            Derivation drv;
            drv.name = drvName;
            drv.outputs.emplace("out", DerivationOutput{DerivationOutput::Deferred{}});
            drv.platform = "x86_64-linux";
            drv.builder = "foo";
            drv.env["out"] = "";
            drv.fillInOutputPaths(*localStore);

            auto drvContents = drv.unparse(*localStore, /*maskOutputs=*/false);

            /* Create an on-disk store object without registering it
               in the SQLite DB. LocalFSStore::getFSAccessor(path, false)
               allows reading store objects based on their filesystem
               presence alone. */
            std::ofstream out(realStoreDir / std::string(drvPath.to_string()), std::ios::binary);
            out.write(drvContents.data(), drvContents.size());
            if (!out)
                throw SysError("writing derivation to store");

            ValidPathInfo info{drvPath, UnkeyedValidPathInfo(*localStore, Hash::dummy)};
            info.narSize = drvContents.size();

            infos.emplace(drvPath, std::move(info));
        }

        state.ResumeTiming();

        localStore->registerValidPaths(infos);

        state.PauseTiming();
        localStore.reset();
        store.reset();
        std::filesystem::remove_all(tmpRoot);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * derivationCount);
}

BENCHMARK(BM_RegisterValidPathsDerivations)->Arg(10)->Arg(50)->Arg(200);

#endif
