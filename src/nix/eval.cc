#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "json.hh"
#include "value-to-json.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdEval : MixJSON, InstallableCommand
{
    bool raw = false;
    std::optional<std::string> apply;

    CmdEval()
    {
        mkFlag(0, "raw", "print strings unquoted", &raw);

        addFlag({
            .longName = "apply",
            .description = "apply a function to each argument",
            .labels = {"expr"},
            .handler = {&apply},
        });
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    Examples examples() override
    {
        return {
            {
                "To evaluate a Nix expression given on the command line:",
                "nix eval --expr '1 + 2'"
            },
            {
                "To evaluate a Nix expression from a file or URI:",
                "nix eval -f ./my-nixpkgs hello.name"
            },
            {
                "To get the current version of Nixpkgs:",
                "nix eval --raw nixpkgs#lib.version"
            },
            {
                "To print the store path of the Hello package:",
                "nix eval --raw nixpkgs#hello"
            },
            {
                "To get a list of checks in the 'nix' flake:",
                "nix eval nix#checks.x86_64-linux --apply builtins.attrNames"
            },
        };
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store) override
    {
        if (raw && json)
            throw UsageError("--raw and --json are mutually exclusive");

        auto state = getEvalState();

        auto v = installable->toValue(*state).first;
        PathSet context;

        if (apply) {
            auto vApply = state->allocValue();
            state->eval(state->parseExprFromString(*apply, absPath(".")), *vApply);
            auto vRes = state->allocValue();
            state->callFunction(*vApply, *v, *vRes, noPos);
            v = vRes;
        }

        if (raw) {
            stopProgressBar();
            std::cout << state->coerceToString(noPos, *v, context);
        } else if (json) {
            JSONPlaceholder jsonOut(std::cout);
            printValueAsJSON(*state, true, *v, jsonOut, context);
        } else {
            state->forceValueDeep(*v);
            logger->stdout("%s", *v);
        }
    }
};

static auto rCmdEval = registerCommand<CmdEval>("eval");
