#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "json.hh"
#include "value-to-json.hh"

using namespace nix;

struct CmdEval : MixJSON, MixInstallables
{
    std::string name() override
    {
        return "eval";
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto jsonOut = json ? std::make_unique<JSONList>(std::cout) : nullptr;

        for (auto & i : installables) {
            auto v = i->toValue(*state);
            if (json) {
                PathSet context;
                auto jsonElem = jsonOut->placeholder();
                printValueAsJSON(*state, true, *v, jsonElem, context);
            } else {
                state->forceValueDeep(*v);
                std::cout << *v << "\n";
            }
        }
    }
};

static RegisterCommand r1(make_ref<CmdEval>());
