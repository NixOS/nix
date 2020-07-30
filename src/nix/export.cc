#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "fs-accessor.hh"

using namespace nix;

struct CmdExport : InstallableCommand
{
    std::string exporter = "github:matthewbauer/nix-bundle";
    Path outLink;

    CmdExport()
    {
        addFlag({
            .longName = "exporter",
            .description = "use custom exporter",
            .labels = {"flake-url"},
            .handler = {&exporter},
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
        return "export an application out of the Nix store";
    }

    Examples examples() override
    {
        return {
            Example{
                "To export Hello:",
                "nix export hello"
            },
        };
    }

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

        auto [exporterFlakeRef, exporterName] = parseFlakeRefWithFragment(exporter, absPath("."));
        const flake::LockFlags lockFlags{ .writeLockFile = false };
        auto exporter = InstallableFlake(
            evalState, std::move(exporterFlakeRef),
            Strings{exporterName == "" ? ("defaultExporter." + settings.thisSystem.get()) : exporterName},
            Strings({"exporters." + settings.thisSystem.get() + "."}), lockFlags);

        Value * arg = evalState->allocValue();
        evalState->mkAttrs(*arg, 1);

        PathSet context;
        for (auto & i : app.context)
            context.insert("=" + store->printStorePath(i.path));
        mkString(*evalState->allocAttr(*arg, evalState->symbols.create("program")), app.program, context);

        auto vRes = evalState->allocValue();
        evalState->callFunction(*exporter.toValue(*evalState).first, *arg, *vRes, noPos);

        if (!evalState->isDerivation(*vRes))
            throw Error("the exporter '%s' does not produce a derivation", exporter.what());

        Bindings::iterator i = vRes->attrs->find(evalState->sDrvPath);
        if (i == vRes->attrs->end())
            throw Error("the exporter '%s' does not produce a derivation", exporter.what());

        PathSet context2;
        StorePath drvPath = store->parseStorePath(evalState->coerceToPath(*i->pos, *i->value, context2));

        i = vRes->attrs->find(evalState->sOutPath);
        if (i == vRes->attrs->end())
            throw Error("the exporter '%s' does not produce a derivation", exporter.what());

        StorePath outPath = store->parseStorePath(evalState->coerceToPath(*i->pos, *i->value, context2));

        store->buildPaths({{drvPath}});

        auto accessor = store->getFSAccessor();
        auto outPathS = store->printStorePath(outPath);
        if (accessor->stat(outPathS).type != FSAccessor::tRegular)
            throw Error("'%s' is not a file; an exporter must only create a single file", outPathS);

        auto info = store->queryPathInfo(outPath);
        if (!info->references.empty())
            throw Error("'%s' has references; an exporter must not leave any references", outPathS);

        if (outLink == "")
            outLink = baseNameOf(app.program);

        store.dynamic_pointer_cast<LocalFSStore>()->addPermRoot(outPath, absPath(outLink), true);
    }
};

static auto r2 = registerCommand<CmdExport>("export");
