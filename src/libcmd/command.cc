#include "command.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "derivations.hh"
#include "nixexpr.hh"
#include "profiles.hh"
#include "repl.hh"

#include <nlohmann/json.hpp>

namespace nix {

HasEvalState::HasEvalState(AbstractArgs & args)
    : MixRepair(args)
    , MixEvalArgs(args)
{
    args.addFlag({
        .longName = "debugger",
        .description = "Start an interactive environment if evaluation fails.",
        .category = MixEvalArgs::category,
        .handler = {&startReplOnEvalErrors, true},
    });
}

HasEvalState::~HasEvalState()
{
    if (evalState)
        evalState->maybePrintStats();
}

ref<Store> HasEvalState::getDrvStore()
{
    if (!drvStore)
        drvStore = evalStoreUrl ? openStore(*evalStoreUrl) : getStore();
    return ref<Store>(drvStore);
}

ref<EvalState> HasEvalState::getEvalState()
{
    if (!evalState) {
        evalState =
            #if HAVE_BOEHMGC
            std::allocate_shared<EvalState>(traceable_allocator<EvalState>(),
                searchPath, getDrvStore(), getStore())
            #else
            std::make_shared<EvalState>(
                searchPath, getDrvStore(), getStore())
            #endif
            ;

        evalState->repair = repair;

        if (startReplOnEvalErrors) {
            evalState->debugRepl = &AbstractNixRepl::runSimple;
        };
    }
    return ref<EvalState>(evalState);
}

}
