#include "nix/cmd/installables.hh"
#include "nix/cmd/installable-derived-path.hh"
#include "store-api.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-cache.hh"
#include "names.hh"
#include "nix/cmd/command.hh"
#include "derivations.hh"

namespace nix {

/**
 * Return the rewrites that are needed to resolve a string whose context is
 * included in `dependencies`.
 */
StringPairs resolveRewrites(
    Store & store,
    const std::vector<BuiltPathWithResult> & dependencies)
{
    StringPairs res;
    for (auto & dep : dependencies)
        if (auto drvDep = std::get_if<BuiltPathBuilt>(&dep.path))
            for (auto & [ outputName, outputPath ] : drvDep->outputs)
                res.emplace(
                    downstreamPlaceholder(store, drvDep->drvPath, outputName),
                    store.printStorePath(outputPath)
                );
    return res;
}

/**
 * Resolve the given string assuming the given context.
 */
std::string resolveString(
    Store & store,
    const std::string & toResolve,
    const std::vector<BuiltPathWithResult> & dependencies)
{
    auto rewrites = resolveRewrites(store, dependencies);
    return rewriteStrings(toResolve, rewrites);
}

UnresolvedApp Installable::toApp(EvalState & state)
{
    auto cursor = getCursor(state);
    auto attrPath = cursor->getAttrPath();

    auto type = cursor->getAttr("type")->getString();

    std::string expected = !attrPath.empty() &&
        (state.symbols[attrPath[0]] == "apps" || state.symbols[attrPath[0]] == "defaultApp")
        ? "app" : "derivation";
    if (type != expected)
        throw Error("attribute '%s' should have type '%s'", cursor->getAttrPathStr(), expected);

    if (type == "app") {
        auto [program, context] = cursor->getAttr("program")->getStringWithContext();

        std::vector<DerivedPath> context2;
        for (auto & c : context) {
            context2.emplace_back(std::visit(overloaded {
                [&](const NixStringContextElem::DrvDeep & d) -> DerivedPath {
                    /* We want all outputs of the drv */
                    return DerivedPath::Built {
                        .drvPath = d.drvPath,
                        .outputs = OutputsSpec::All {},
                    };
                },
                [&](const NixStringContextElem::Built & b) -> DerivedPath {
                    return DerivedPath::Built {
                        .drvPath = b.drvPath,
                        .outputs = OutputsSpec::Names { b.output },
                    };
                },
                [&](const NixStringContextElem::Opaque & o) -> DerivedPath {
                    return DerivedPath::Opaque {
                        .path = o.path,
                    };
                },
            }, c.raw()));
        }

        return UnresolvedApp{App {
            .context = std::move(context2),
            .program = program,
        }};
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state.sOutPath)->getString();
        auto outputName = cursor->getAttr(state.sOutputName)->getString();
        auto name = cursor->getAttr(state.sName)->getString();
        auto aPname = cursor->maybeGetAttr("pname");
        auto aMeta = cursor->maybeGetAttr(state.sMeta);
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr("mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString()
            : aPname
            ? aPname->getString()
            : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        return UnresolvedApp { App {
            .context = { DerivedPath::Built {
                .drvPath = drvPath,
                .outputs = OutputsSpec::Names { outputName },
            } },
            .program = program,
        }};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", cursor->getAttrPathStr(), type);
}

// FIXME: move to libcmd
App UnresolvedApp::resolve(ref<Store> evalStore, ref<Store> store)
{
    auto res = unresolved;

    std::vector<std::shared_ptr<Installable>> installableContext;

    for (auto & ctxElt : unresolved.context)
        installableContext.push_back(
            std::make_shared<InstallableDerivedPath>(store, DerivedPath { ctxElt }));

    auto builtContext = Installable::build(evalStore, store, Realise::Outputs, installableContext);
    res.program = resolveString(*store, unresolved.program, builtContext);
    if (!store->isInStore(res.program))
        throw Error("app program '%s' is not in the Nix store", res.program);

    return res;
}

}
