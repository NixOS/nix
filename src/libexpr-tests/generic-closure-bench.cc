#include <benchmark/benchmark.h>

#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/store-open.hh"

using namespace nix;

namespace {

constexpr size_t genericClosureOutDegree = 8;

static void evalExpr(benchmark::State & state, const std::string & exprStr, size_t itemsProcessed)
{
    for (auto _ : state) {
        state.PauseTiming();

        auto store = openStore("dummy://");
        fetchers::Settings fetchSettings{};
        bool readOnlyMode = true;
        EvalSettings evalSettings{readOnlyMode};
        evalSettings.nixPath = {};

        EvalState st({}, store, fetchSettings, evalSettings, nullptr);
        Expr * expr = st.parseExprFromString(exprStr, st.rootPath(CanonPath::root));

        Value v;

        state.ResumeTiming();

        st.eval(expr, v);
        st.forceValue(v, noPos);
        benchmark::DoNotOptimize(v);
    }

    state.SetItemsProcessed(state.iterations() * itemsProcessed);
}

static std::string mkGenericClosureIntKeysExpr(size_t nodeCount)
{
    std::string res;
    res.reserve(1024);

    res += "let\n";
    res += "  N = ";
    res += std::to_string(nodeCount);
    res += ";\n";
    res += "  mod = a: b: a - b * (builtins.div a b);\n";
    res += "  nodes = builtins.genList (n: { key = n; }) N;\n";
    res += "in builtins.genericClosure {\n";
    res += "  startSet = [ (builtins.elemAt nodes 0) ];\n";
    res += "  operator = x:\n";
    res += "    let k = x.key; in [\n";
    for (size_t i = 1; i <= genericClosureOutDegree; ++i) {
        res += "      (builtins.elemAt nodes (mod (k + ";
        res += std::to_string(i);
        res += ") N))\n";
    }
    res += "    ];\n";
    res += "}\n";

    return res;
}

static std::string mkGenericClosureStringKeysExpr(size_t nodeCount)
{
    std::string res;
    res.reserve(1024);

    res += "let\n";
    res += "  N = ";
    res += std::to_string(nodeCount);
    res += ";\n";
    res += "  mod = a: b: a - b * (builtins.div a b);\n";
    res += "  keys = builtins.genList builtins.toString N;\n";
    res += "  nodes = builtins.genList (n: { key = builtins.elemAt keys n; i = n; }) N;\n";
    res += "in builtins.genericClosure {\n";
    res += "  startSet = [ (builtins.elemAt nodes 0) ];\n";
    res += "  operator = x:\n";
    res += "    let k = x.i; in [\n";
    for (size_t i = 1; i <= genericClosureOutDegree; ++i) {
        res += "      (builtins.elemAt nodes (mod (k + ";
        res += std::to_string(i);
        res += ") N))\n";
    }
    res += "    ];\n";
    res += "}\n";

    return res;
}

static void BM_GenericClosure_IntKeys(benchmark::State & state)
{
    const auto nodeCount = static_cast<size_t>(state.range(0));
    const auto exprStr = mkGenericClosureIntKeysExpr(nodeCount);
    evalExpr(state, exprStr, nodeCount);
}

static void BM_GenericClosure_StringKeys(benchmark::State & state)
{
    const auto nodeCount = static_cast<size_t>(state.range(0));
    const auto exprStr = mkGenericClosureStringKeysExpr(nodeCount);
    evalExpr(state, exprStr, nodeCount);
}

BENCHMARK(BM_GenericClosure_IntKeys)->Arg(1'000)->Arg(5'000)->Arg(20'000);
BENCHMARK(BM_GenericClosure_StringKeys)->Arg(1'000)->Arg(5'000)->Arg(20'000);

} // namespace
