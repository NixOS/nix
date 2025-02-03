#include "command-installable-value.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "value-to-json.hh"
#include "progress-bar.hh"

#include <nlohmann/json.hpp>

using namespace nix;

namespace nix::fs { using namespace std::filesystem; }

struct CmdEval : MixJSON, InstallableValueCommand, MixReadOnlyOption
{
    bool raw = false;
    std::optional<std::string> apply;
    std::optional<fs::path> writeTo;

    CmdEval() : InstallableValueCommand()
    {
        addFlag({
            .longName = "raw",
            .description = "Print strings without quotes or escaping.",
            .handler = {&raw, true},
        });

        addFlag({
            .longName = "apply",
            .description = "Apply the function *expr* to each argument.",
            .labels = {"expr"},
            .handler = {&apply},
        });

        addFlag({
            .longName = "write-to",
            .description = "Write a string or attrset of strings to *path*.",
            .labels = {"path"},
            .handler = {&writeTo},
        });
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    std::string doc() override
    {
        return
          #include "eval.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        if (raw && json)
            throw UsageError("--raw and --json are mutually exclusive");

        auto state = getEvalState();

        auto [v, pos] = installable->toValue(*state);
        NixStringContext context;

        if (apply) {
            auto vApply = state->allocValue();
            state->eval(state->parseExprFromString(*apply, state->rootPath(".")), *vApply);
            auto vRes = state->allocValue();
            state->callFunction(*vApply, *v, *vRes, noPos);
            v = vRes;
        }

        if (writeTo) {
            stopProgressBar();

            if (fs::symlink_exists(*writeTo))
                throw Error("path '%s' already exists", writeTo->string());

            std::function<void(Value & v, const PosIdx pos, const fs::path & path)> recurse;

            recurse = [&](Value & v, const PosIdx pos, const fs::path & path)
            {
                state->forceValue(v, pos);
                if (v.type() == nString)
                    // FIXME: disallow strings with contexts?
                    writeFile(path.string(), v.string_view());
                else if (v.type() == nAttrs) {
                    [[maybe_unused]] bool directoryCreated = fs::create_directory(path);
                    // Directory should not already exist
                    assert(directoryCreated);
                    for (auto & attr : *v.attrs()) {
                        std::string_view name = state->symbols[attr.name];
                        try {
                            if (name == "." || name == "..")
                                throw Error("invalid file name '%s'", name);
                            recurse(*attr.value, attr.pos, path / name);
                        } catch (Error & e) {
                            e.addTrace(
                                state->positions[attr.pos],
                                HintFmt("while evaluating the attribute '%s'", name));
                            throw;
                        }
                    }
                }
                else
                    state->error<TypeError>("value at '%s' is not a string or an attribute set", state->positions[pos]).debugThrow();
            };

            recurse(*v, pos, *writeTo);
        }

        else if (raw) {
            stopProgressBar();
            writeFull(getStandardOutput(), *state->coerceToString(noPos, *v, context, "while generating the eval command output"));
        }

        else if (json) {
            logger->cout("%s", printValueAsJSON(*state, true, *v, pos, context, false));
        }

        else {
            logger->cout(
                "%s",
                ValuePrinter(
                    *state,
                    *v,
                    PrintOptions {
                        .force = true,
                        .derivationPaths = true
                    }
                )
            );
        }
    }
};

static auto rCmdEval = registerCommand<CmdEval>("eval");
