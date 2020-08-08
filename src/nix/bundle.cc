#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "fs-accessor.hh"

using namespace nix;

struct CmdBundle : InstallableCommand
{
    std::string bundler = "github:matthewbauer/nix-bundle";
    std::optional<Path> outLink;

    CmdBundle()
    {
        addFlag({
            .longName = "bundler",
            .description = "use custom bundler",
            .labels = {"flake-url"},
            .handler = {&bundler},
            .completer = {[&](size_t, std::string_view prefix) {
                completeFlakeRef(getStore(), prefix);
            }}
        });

        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "path of the symlink to the build result",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "bundle an application so that it works outside of the Nix store";
    }

    Examples examples() override
    {
        return {
            Example{
                "To bundle Hello:",
                "nix bundle hello"
            },
        };
    }

    Category category() override { return catSecondary; }

    Strings getDefaultFlakeAttrPaths() override
    {
        Strings res{"defaultApp." + settings.thisSystem.get()};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPaths())
            res.push_back(s);
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"apps." + settings.thisSystem.get() + ".", "packages"};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPathPrefixes())
            res.push_back(s);
        return res;
    }

    void run(ref<Store> store) override
    {
        auto evalState = getEvalState();

        auto app = installable->toApp(*evalState);
        store->buildPaths(app.context);

        auto [bundlerFlakeRef, bundlerName] = parseFlakeRefWithFragment(bundler, absPath("."));
        const flake::LockFlags lockFlags{ .writeLockFile = false };
        auto bundler = InstallableFlake(
            evalState, std::move(bundlerFlakeRef),
            Strings{bundlerName == "" ? "defaultBundler" : bundlerName},
            Strings({"bundlers."}), lockFlags);

        Value * arg = evalState->allocValue();
        evalState->mkAttrs(*arg, 2);

        PathSet context;
        for (auto & i : app.context)
            context.insert("=" + store->printStorePath(i.path));
        mkString(*evalState->allocAttr(*arg, evalState->symbols.create("program")), app.program, context);

        mkString(*evalState->allocAttr(*arg, evalState->symbols.create("system")), settings.thisSystem.get());

        arg->attrs->sort();
 
        auto vRes = evalState->allocValue();
        evalState->callFunction(*bundler.toValue(*evalState).first, *arg, *vRes, noPos);

        if (!evalState->isDerivation(*vRes))
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        auto attr1 = vRes->attrs->find(evalState->sDrvPath);
        if (!attr1)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        PathSet context2;
        StorePath drvPath = store->parseStorePath(evalState->coerceToPath(*attr1->pos, *attr1->value, context2));

        auto attr2 = vRes->attrs->find(evalState->sOutPath);
        if (!attr2)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        StorePath outPath = store->parseStorePath(evalState->coerceToPath(*attr2->pos, *attr2->value, context2));

        store->buildPaths({{drvPath}});

        auto outPathS = store->printStorePath(outPath);

        auto info = store->queryPathInfo(outPath);
        if (!info->references.empty())
            throw Error("'%s' has references; a bundler must not leave any references", outPathS);

        if (!outLink)
            outLink = baseNameOf(app.program);

        store.dynamic_pointer_cast<LocalFSStore>()->addPermRoot(outPath, absPath(*outLink), true);
    }
};

static auto r2 = registerCommand<CmdBundle>("bundle");
