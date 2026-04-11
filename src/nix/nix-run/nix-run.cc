#include "nix/cmd/common-eval-args.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/cmd/legacy.hh"
#include "nix/expr/attr-path.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/eval.hh"
#include "nix/main/shared.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/util/util.hh"

#include "../run.hh"
#include "man-pages.hh"

#include <iostream>

namespace nix {

static void showHelp()
{
    std::cout <<
        R"(Usage: nix-run [OPTIONS] [FILE] [-- ARGS...]

Evaluate FILE (default: ./default.nix) and run the executable selected
by `nix run`-style `getExe` semantics: `meta.mainProgram`, falling back
to `pname`, and finally to the parsed derivation name.

Options:
  -A, --attr ATTR     Select the given attribute path inside the
                      evaluated expression (e.g. `hello`, `pkgs.hello`).
  -E, --expr          Interpret the positional argument as a Nix
                      expression instead of a file path.
  --                  End of options; remaining arguments are passed
                      verbatim to the program being run.

Examples:
  nix-run '<nixpkgs>' -A hello
  nix-run -E 'import <nixpkgs> {}' -A hello -- --greeting=hi
  nix-run ./release.nix -A myApp
)";
}

static int main_nix_run(int argc, char ** argv)
{
    Strings attrPaths;
    Strings positionals;
    bool fromArgs = false;
    bool readStdin = false;
    bool showHelpAndExit = false;

    /* Argument layout:
         nix-run [OPTIONS] [FILE | --expr EXPR | -] [PROGRAM ARGS...]
       The first positional names the source (a file path, or an
       expression if `-E` is used, or `-` for stdin). Every subsequent
       positional is forwarded verbatim to the program. A literal `--`
       is consumed by the base argument parser, which then routes all
       remaining arguments through `processArgs` as positionals. */
    struct MyArgs : LegacyArgs, MixEvalArgs
    {
        using LegacyArgs::LegacyArgs;

        Strings * positionals = nullptr;

        bool processArgs(const Strings & args, bool finish) override
        {
            for (auto & a : args)
                positionals->push_back(a);
            return true;
        }
    };

    auto parseFlag = [&](Strings::iterator & arg, const Strings::iterator & end) -> bool {
        if (*arg == "--help")
            showHelpAndExit = true;
        else if (*arg == "--version")
            printVersion("nix-run");
        else if (*arg == "--attr" || *arg == "-A")
            attrPaths.push_back(getArg(*arg, arg, end));
        else if (*arg == "--expr" || *arg == "-E")
            fromArgs = true;
        else if (*arg == "-")
            readStdin = true;
        else if (*arg != "" && arg->at(0) == '-')
            return false;
        else
            /* Bare positionals are dispatched through `processArgs`, not
               `processFlag`, so this branch is unreachable. Kept for
               defence in depth in case the dispatch ever changes. */
            positionals.push_back(*arg);
        return true;
    };

    MyArgs myArgs(std::string(baseNameOf(argv[0])), parseFlag);
    myArgs.positionals = &positionals;

    myArgs.parseCmdline(argvToStrings(argc, argv));

    if (showHelpAndExit) {
        showHelp();
        return 0;
    }

    auto store = openStore();
    auto evalStore = myArgs.evalStoreUrl ? openStore(StoreReference{*myArgs.evalStoreUrl}) : store;

    auto state = std::make_shared<EvalState>(myArgs.lookupPath, evalStore, fetchSettings, evalSettings, store);
    state->repair = myArgs.repair;

    Bindings & autoArgs = *myArgs.getAutoArgs(*state);

    /* The first positional (if any) selects the source; everything
       after it is forwarded to the program. */
    std::optional<std::string> source;
    Strings programArgs;
    if (!positionals.empty()) {
        source = positionals.front();
        positionals.pop_front();
    }
    programArgs = std::move(positionals);

    if (readStdin && (source || fromArgs))
        throw UsageError("nix-run: '-' cannot be combined with a file or '--expr'");
    if (fromArgs && !source)
        throw UsageError("nix-run: '--expr' requires an argument");

    /* Parse the source expression to evaluate. */
    Expr * e;
    if (readStdin) {
        e = state->parseStdin();
    } else if (fromArgs) {
        e = state->parseExprFromString(*source, state->rootPath("."));
    } else {
        auto sourcePath = lookupFileArg(*state, source.value_or("./default.nix"));
        e = state->parseExprFromFile(resolveExprPath(sourcePath));
    }

    Value vRoot;
    state->eval(e, vRoot);

    if (attrPaths.empty())
        attrPaths = {""};
    if (attrPaths.size() > 1)
        throw UsageError("nix-run accepts at most one '--attr' path");

    Value & vPkg = *findAlongAttrPath(*state, attrPaths.front(), autoArgs, vRoot).first;
    state->forceValue(vPkg, vPkg.determinePos(noPos));

    /* `nix run`-style executable selection, mirroring `lib.getExe`:
       prefer `meta.mainProgram`, then `pname`, then the parsed
       derivation name. */
    auto getExeExpr = state->parseExprFromString(
        R"RAW(pkg: "${pkg}/bin/${
          pkg.meta.mainProgram
            or (pkg.pname
              or (builtins.parseDrvName pkg.name).name)
        }")RAW",
        state->rootPath("."));

    Value vGetExe;
    state->eval(getExeExpr, vGetExe);

    Value vProgram;
    state->callFunction(vGetExe, vPkg, vProgram, noPos);

    NixStringContext context;
    auto program =
        state->coerceToString(noPos, vProgram, context, "while evaluating the program path for nix-run", false, false)
            .toOwned();

    /* Turn the string context into derived paths, then let UnresolvedApp
       build them and resolve placeholders (for CA derivations). This is
       the same machinery `nix run` uses via InstallableValue::toApp. */
    std::vector<DerivedPath> context2;
    for (auto & c : context) {
        context2.emplace_back(
            std::visit(
                overloaded{
                    [&](const NixStringContextElem::DrvDeep & d) -> DerivedPath {
                        return DerivedPath::Built{
                            .drvPath = makeConstantStorePathRef(d.drvPath),
                            .outputs = OutputsSpec::All{},
                        };
                    },
                    [&](const NixStringContextElem::Built & b) -> DerivedPath {
                        return DerivedPath::Built{
                            .drvPath = b.drvPath,
                            .outputs = OutputsSpec::Names{b.output},
                        };
                    },
                    [&](const NixStringContextElem::Opaque & o) -> DerivedPath {
                        return DerivedPath::Opaque{.path = o.path};
                    },
                },
                c.raw));
    }

    UnresolvedApp unresolvedApp{App{
        .context = std::move(context2),
        .program = program,
    }};
    auto app = unresolvedApp.resolve(evalStore, store);

    Strings allArgs{app.program.string()};
    for (auto & a : programArgs)
        allArgs.push_back(std::move(a));

    state->maybePrintStats();

    /* Release eval caches before exec'ing, matching `nix run`. */
    state->evalCaches.clear();

    execProgramInStore(store, UseLookupPath::DontUse, app.program.string(), allArgs);

    return 0;
}

static RegisterLegacyCommand r_nix_run("nix-run", main_nix_run);

} // namespace nix
