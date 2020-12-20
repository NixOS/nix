#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "json.hh"
#include "value-to-json.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdEval : MixJSON, InstallableCommand
{
    bool raw = false;
    std::optional<std::string> apply;
    std::optional<Path> writeTo;

    CmdEval()
    {
        mkFlag(0, "raw", "print strings unquoted", &raw);

        addFlag({
            .longName = "apply",
            .description = "apply a function to each argument",
            .labels = {"expr"},
            .handler = {&apply},
        });

        addFlag({
            .longName = "write-to",
            .description = "write a string or attrset of strings to 'path'",
            .labels = {"path"},
            .handler = {&writeTo},
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

        auto [v, pos] = installable->toValue(*state);
        PathSet context;

        if (apply) {
            auto vApply = state->allocValue();
            state->eval(state->parseExprFromString(*apply, absPath(".")), *vApply);
            auto vRes = state->allocValue();
            state->callFunction(*vApply, *v, *vRes, noPos);
            v = vRes;
        }

        if (writeTo) {
            stopProgressBar();

            if (pathExists(*writeTo))
                throw Error("path '%s' already exists", *writeTo);

            std::function<void(Value & v, const Pos & pos, const Path & path)> recurse;

            recurse = [&](Value & v, const Pos & pos, const Path & path)
            {
                state->forceValue(v);
                if (v.type == tString)
                    // FIXME: disallow strings with contexts?
                    writeFile(path, v.string.s);
                else if (v.type == tAttrs) {
                    if (mkdir(path.c_str(), 0777) == -1)
                        throw SysError("creating directory '%s'", path);
                    for (auto & attr : *v.attrs)
                        try {
                            if (attr.name == "." || attr.name == "..")
                                throw Error("invalid file name '%s'", attr.name);
                            recurse(*attr.value, *attr.pos, path + "/" + std::string(attr.name));
                        } catch (Error & e) {
                            e.addTrace(*attr.pos, hintfmt("while evaluating the attribute '%s'", attr.name));
                            throw;
                        }
                }
                else
                    throw TypeError("value at '%s' is not a string or an attribute set", pos);
            };

            recurse(*v, pos, *writeTo);
        }

        else if (raw) {
            stopProgressBar();
            std::cout << state->coerceToString(noPos, *v, context);
        }

        else if (json) {
            JSONPlaceholder jsonOut(std::cout);
            printValueAsJSON(*state, true, *v, jsonOut, context);
        }

        else {
            state->forceValueDeep(*v);
            logger->cout("%s", *v);
        }
    }
};

static auto rCmdEval = registerCommand<CmdEval>("eval");
