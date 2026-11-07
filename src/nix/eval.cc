#include "nix/cmd/command-installable-value.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/util/archive.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/system-error.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct CmdEval : MixJSON, InstallableValueCommand, MixReadOnlyOption
{
    bool raw = false;
    std::optional<std::string> apply;
    std::optional<std::filesystem::path> writeTo;

    CmdEval()
        : InstallableValueCommand()
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

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        if (raw && json)
            throw UsageError("--raw and --json are mutually exclusive");

        auto state = getEvalState();

        auto [v, pos] = installable->toValue(*state, AutoCall::No);
        NixStringContext context;

        if (apply) {
            auto vApply = state->allocValue();
            state->eval(state->parseExprFromString(*apply, state->rootPath(".")), *vApply);
            auto vRes = state->allocValue();
            state->callFunction(*vApply, *v, *vRes, noPos);
            v = vRes;
        }

        if (writeTo) {
            logger->stop();

            /* As with POSIX, a trailing separator (e.g. `out/`) means
               the destination must be a directory. `RestoreSink` wants
               it without one.

               TODO: Handle this in `RestoreSink::forPath` instead. */
            auto dstPath = *writeTo;
            if (!writeTo->has_filename() && writeTo->has_relative_path()) {
                dstPath = writeTo->parent_path();
                state->forceValue(*v, pos);
                if (v->type() != nAttrs)
                    throw SysError(EISDIR, "writing to %s", PathFmt(*writeTo));
            }

            /* `RestoreSink` creates everything relative to descriptors,
               refusing to overwrite anything or follow symlinks. */
            auto restoreSink = RestoreSink::forPath(dstPath, /*startFsync=*/false);

            [&](this const auto & recurse, FileSystemObjectSink & sink, Value & v, const PosIdx pos, size_t depth)
                -> void {
                /* Bound the recursion, and only write what could also be
                   dumped as a NAR. */
                if (depth >= narMaxDepth)
                    throw Error("attribute set nesting exceeds maximum depth of %d", narMaxDepth);
                state->forceValue(v, pos);
                if (v.type() == nString) {
                    copyContext(v, context);
                    sink.createRegularFile(
                        /*isExecutable=*/false,
                        [&](FileSystemObjectSink::OnRegularFile & crf) { crf(v.string_view()); });
                } else if (v.type() == nAttrs) {
                    sink.createDirectory([&](FileSystemObjectSink::OnDirectory & dirSink) {
                        for (auto & attr : *v.attrs()) {
                            std::string_view name = state->symbols[attr.name];
                            try {
                                /* Each name must be a single path component. */
                                if (name.empty() || name == "." || name == ".." || name.find('/') != name.npos)
                                    throw Error("invalid file name '%s'", name);
                                dirSink.createChild(name, [&](FileSystemObjectSink & childSink) {
                                    recurse(childSink, *attr.value, attr.pos, depth + 1);
                                });
                            } catch (Error & e) {
                                e.addTrace(
                                    state->positions[attr.pos], HintFmt("while evaluating the attribute '%s'", name));
                                throw;
                            }
                        }
                    });
                } else
                    state->error<TypeError>("value at '%s' is not a string or an attribute set", state->positions[pos])
                        .debugThrow();
            }(restoreSink, *v, pos, 0);
        }

        else if (raw) {
            logger->stop();
            auto string = state->coerceToString(noPos, *v, context, "while generating the eval command output");
            writeFull(getStandardOutput(), *string);
        }

        else if (json) {
            printJSON(printValueAsJSON(*state, true, *v, pos, context, false));
        }

        else {
            ValuePrinter printer(*state, *v, PrintOptions{.force = true, .derivationPaths = true}, &context);
            logger->cout("%s", printer);
        }

        state->ensureLazyPathsCopied(context);
    }
};

static auto rCmdEval = registerCommand<CmdEval>("eval");

} // namespace nix
