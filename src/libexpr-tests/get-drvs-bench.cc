#include <benchmark/benchmark.h>

#include "nix/expr/get-drvs.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/store-open.hh"
#include "nix/util/fmt.hh"

using namespace nix;

namespace {

struct GetDerivationsEnv
{
    ref<Store> store = openStore("dummy://");
    fetchers::Settings fetchSettings{};
    bool readOnlyMode = true;
    EvalSettings evalSettings{readOnlyMode};
    std::shared_ptr<EvalState> statePtr;
    EvalState & state;

    Bindings * autoArgs = nullptr;
    Value attrsValue;

    explicit GetDerivationsEnv(size_t attrCount)
        : evalSettings([&]() {
            EvalSettings settings{readOnlyMode};
            settings.nixPath = {};
            return settings;
        }())
        , statePtr(std::make_shared<EvalState>(LookupPath{}, store, fetchSettings, evalSettings, nullptr))
        , state(*statePtr)
    {
        autoArgs = state.buildBindings(0).finish();

        auto attrs = state.buildBindings(attrCount);

        for (size_t i = 0; i < attrCount; ++i) {
            auto name = fmt("pkg%|1$06d|", i);
            auto sym = state.symbols.create(name);
            auto & v = attrs.alloc(sym);
            v.mkInt(i);
        }

        attrsValue.mkAttrs(attrs.finish());
    }
};

} // namespace

static void BM_GetDerivationsAttrScan(benchmark::State & state)
{
    const auto attrCount = static_cast<size_t>(state.range(0));
    GetDerivationsEnv env(attrCount);

    for (auto _ : state) {
        PackageInfos drvs;
        getDerivations(
            env.state, env.attrsValue, /*pathPrefix=*/"", *env.autoArgs, drvs, /*ignoreAssertionFailures=*/true);
        benchmark::DoNotOptimize(drvs.size());
    }

    state.SetItemsProcessed(state.iterations() * attrCount);
}

BENCHMARK(BM_GetDerivationsAttrScan)->Arg(1'000)->Arg(5'000)->Arg(10'000);
