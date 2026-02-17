#include <benchmark/benchmark.h>

#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/store-open.hh"

using namespace nix;

static void BM_EvalManyBuiltinsMatchSameRegex(benchmark::State & state)
{
    static constexpr int iterations = 5'000;

    static constexpr std::string_view exprStr =
        "builtins.foldl' "
        "(acc: _: acc + builtins.length (builtins.match \"a\" \"a\")) "
        "0 "
        "(builtins.genList (x: x) "
        "5000)";

    for (auto _ : state) {
        state.PauseTiming();

        auto store = openStore("dummy://");
        fetchers::Settings fetchSettings{};
        bool readOnlyMode = true;
        EvalSettings evalSettings{readOnlyMode};
        evalSettings.nixPath = {};

        auto stPtr = std::make_shared<EvalState>(LookupPath{}, store, fetchSettings, evalSettings, nullptr);
        auto & st = *stPtr;
        Expr * expr = st.parseExprFromString(std::string(exprStr), st.rootPath(CanonPath::root));

        Value v;

        state.ResumeTiming();

        st.eval(expr, v);
        st.forceValue(v, noPos);
        benchmark::DoNotOptimize(v);
    }

    state.SetItemsProcessed(state.iterations() * iterations);
}

BENCHMARK(BM_EvalManyBuiltinsMatchSameRegex);
