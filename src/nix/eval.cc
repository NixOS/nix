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

    CmdEval()
    {
        mkFlag(0, "raw", "print strings unquoted", &raw);
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    Examples examples() override
    {
        return {
            Example{
                "To evaluate a Nix expression given on the command line:",
                "nix eval '(1 + 2)'"
            },
            Example{
                "To evaluate a Nix expression from a file or URI:",
                "nix eval -f channel:nixos-17.09 hello.name"
            },
            Example{
                "To get the current version of Nixpkgs:",
                "nix eval --raw nixpkgs.lib.version"
            },
            Example{
                "To print the store path of the Hello package:",
                "nix eval --raw nixpkgs.hello"
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

static auto r1 = registerCommand<CmdEval>("eval");
