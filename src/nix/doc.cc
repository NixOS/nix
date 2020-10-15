#include "command.hh"
#include "common-args.hh"
#include "eval-cache.hh"
#include "eval-inline.hh"
#include "local-fs-store.hh"

using namespace nix;
using namespace nix::flake;

// FIXME: move
StorePath buildDerivation(EvalState & state, Value & vDerivation)
{
    state.forceValue(vDerivation);
    if (!state.isDerivation(vDerivation))
        throw Error("value did not evaluate to a derivation");

    auto aDrvPath = vDerivation.attrs->get(state.sDrvPath);
    assert(aDrvPath);
    PathSet context;
    auto drvPath = state.store->parseStorePath(state.coerceToPath(*aDrvPath->pos, *aDrvPath->value, context));

    state.store->buildPaths({{drvPath}});

    auto aOutPath = vDerivation.attrs->get(state.sOutPath);
    assert(aOutPath);
    auto outPath = state.store->parseStorePath(state.coerceToPath(*aOutPath->pos, *aOutPath->value, context));

    assert(state.store->isValidPath(outPath));

    return outPath;
}

struct CmdDoc : FlakeCommand
{
    std::optional<Path> outLink = "flake-doc";
    bool printMarkdown = false;

    CmdDoc()
    {
        // FIXME: cut&paste from 'nix build'.
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "path of the symlink to the build result",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

        addFlag({
            .longName = "no-link",
            .description = "do not create a symlink to the build result",
            .handler = {&outLink, {}},
        });

        addFlag({
            .longName = "print-markdown",
            .description = "show markdown, don't generate an HTML book",
            .handler = {&this->printMarkdown, true},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto state = getEvalState();
        auto flake = std::make_shared<LockedFlake>(lockFlake());

        auto vFlake = state->allocValue();
        flake::callFlake(*state, *flake, *vFlake);

        auto vFun = state->allocValue();
        state->eval(state->parseExprFromString(
            #include "doc.nix.gen.hh"
            , "/"), *vFun);

        auto vRes = state->allocValue();
        state->callFunction(*vFun, *vFlake, *vRes, noPos);
        state->forceAttrs(*vRes, noPos);

        auto markdown = vRes->attrs->get(state->symbols.create("markdown"));
        assert(markdown);

        if (printMarkdown) {
            logger->stdout(state->forceString(*markdown->value));
            return;
        }

        auto mdbook = vRes->attrs->get(state->symbols.create("mdbook"));
        assert(mdbook);

        // FIXME: ugly, needed for getFlake.
        evalSettings.pureEval = false;

        auto path = buildDerivation(*state, *mdbook->value);

        if (outLink)
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                store2->addPermRoot(path, absPath(*outLink));
    }
};

static auto r1 = registerCommand<CmdDoc>("doc");
