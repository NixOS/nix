#include "installable-flake.hh"
#include "command-installable-value.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "fs-accessor.hh"
#include "eval-inline.hh"

using namespace nix;

struct CmdBundle : InstallableValueCommand
{
    std::string bundler = "github:NixOS/bundlers";
    std::optional<Path> outLink;

    CmdBundle()
    {
        addFlag({
            .longName = "bundler",
            .description = fmt("Use a custom bundler instead of the default (`%s`).", bundler),
            .labels = {"flake-url"},
            .handler = {&bundler},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }}
        });

        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Override the name of the symlink to the build result. It defaults to the base name of the app.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

    }

    std::string description() override
    {
        return "bundle an application so that it works outside of the Nix store";
    }

    std::string doc() override
    {
        return
          #include "bundle.md"
          ;
    }

    Category category() override { return catSecondary; }

    // FIXME: cut&paste from CmdRun.
    Strings getDefaultFlakeAttrPaths() override
    {
        Strings res{
            "apps." + settings.thisSystem.get() + ".default",
            "defaultApp." + settings.thisSystem.get()
        };
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPaths())
            res.push_back(s);
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"apps." + settings.thisSystem.get() + "."};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPathPrefixes())
            res.push_back(s);
        return res;
    }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        auto evalState = getEvalState();

        auto val = installable->toValue(*evalState).first;

        auto [bundlerFlakeRef, bundlerName, extendedOutputsSpec] = parseFlakeRefWithFragmentAndExtendedOutputsSpec(bundler, absPath("."));
        const flake::LockFlags lockFlags{ .writeLockFile = false };
        InstallableFlake bundler{this,
            evalState, std::move(bundlerFlakeRef), bundlerName, std::move(extendedOutputsSpec),
            {"bundlers." + settings.thisSystem.get() + ".default",
             "defaultBundler." + settings.thisSystem.get()
            },
            {"bundlers." + settings.thisSystem.get() + "."},
            lockFlags
        };

        auto vRes = evalState->allocValue();
        evalState->callFunction(*bundler.toValue(*evalState).first, *val, *vRes, noPos);

        if (!evalState->isDerivation(*vRes))
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        auto attr1 = vRes->attrs->get(evalState->sDrvPath);
        if (!attr1)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        NixStringContext context2;
        auto drvPath = evalState->coerceToStorePath(attr1->pos, *attr1->value, context2, "");

        auto attr2 = vRes->attrs->get(evalState->sOutPath);
        if (!attr2)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        auto outPath = evalState->coerceToStorePath(attr2->pos, *attr2->value, context2, "");

        store->buildPaths({
            DerivedPath::Built {
                .drvPath = makeConstantStorePathRef(drvPath),
                .outputs = OutputsSpec::All { },
            },
        });

        auto outPathS = store->printStorePath(outPath);

        if (!outLink) {
            auto * attr = vRes->attrs->get(evalState->sName);
            if (!attr)
                throw Error("attribute 'name' missing");
            outLink = evalState->forceStringNoCtx(*attr->value, attr->pos, "");
        }

        // TODO: will crash if not a localFSStore?
        store.dynamic_pointer_cast<LocalFSStore>()->addPermRoot(outPath, absPath(*outLink));
    }
};

static auto r2 = registerCommand<CmdBundle>("bundle");
