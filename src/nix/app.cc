#include "nix/cmd/installables.hh"
#include "nix/cmd/installable-derived-path.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/store/store-api.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/store/names.hh"
#include "nix/cmd/command.hh"
#include "nix/store/derivations.hh"
#include "nix/store/downstream-placeholder.hh"

namespace nix {

/**
 * Return the rewrites that are needed to resolve a string whose context is
 * included in `dependencies`.
 */
StringPairs resolveRewrites(Store & store, const std::vector<BuiltPathWithResult> & dependencies)
{
    StringPairs res;
    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        return res;
    }
    for (auto & dep : dependencies) {
        auto drvDep = std::get_if<BuiltPathBuilt>(&dep.path);
        if (!drvDep) {
            continue;
        }

        for (const auto & [outputName, outputPath] : drvDep->outputs) {
            res.emplace(
                DownstreamPlaceholder::fromSingleDerivedPathBuilt(
                    SingleDerivedPath::Built{
                        .drvPath = make_ref<SingleDerivedPath>(drvDep->drvPath->discardOutputPath()),
                        .output = outputName,
                    })
                    .render(),
                store.printStorePath(outputPath));
        }
    }
    return res;
}

/**
 * Resolve the given string assuming the given context.
 */
std::string
resolveString(Store & store, const std::string & toResolve, const std::vector<BuiltPathWithResult> & dependencies)
{
    auto rewrites = resolveRewrites(store, dependencies);
    return rewriteStrings(toResolve, rewrites);
}

UnresolvedApp InstallableValue::toApp(EvalState & state)
{
    auto cursor = getCursor(state);
    auto attrPath = cursor->getAttrPath();

    auto type = cursor->getAttr("type")->getString();

    std::string expectedType =
        !attrPath.empty() && (state.symbols[attrPath[0]] == "apps" || state.symbols[attrPath[0]] == "defaultApp")
            ? "app"
            : "derivation";
    if (type != expectedType)
        throw Error("attribute '%s' should have type '%s'", cursor->getAttrPathStr(), expectedType);

    if (type == "app") {
        auto [program, context] = cursor->getAttr("program")->getStringWithContext();

        std::vector<DerivedPath> context2;
        for (auto & c : context) {
            context2.emplace_back(
                std::visit(
                    overloaded{
                        [&](const NixStringContextElem::DrvDeep & d) -> DerivedPath {
                            /* We want all outputs of the drv */
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
                            return DerivedPath::Opaque{
                                .path = o.path,
                            };
                        },
                    },
                    c.raw));
        }

        return UnresolvedApp{App{
            .context = std::move(context2),
            .program = program,
        }};
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state.s.outPath)->getString();
        auto outputName = cursor->getAttr(state.s.outputName)->getString();
        auto name = cursor->getAttr(state.s.name)->getString();
        auto aPname = cursor->maybeGetAttr("pname");
        auto aMeta = cursor->maybeGetAttr(state.s.meta);
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr("mainProgram") : nullptr;
        auto mainProgram = aMainProgram ? aMainProgram->getString() : aPname ? aPname->getString() : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        return UnresolvedApp{App{
            .context = {DerivedPath::Built{
                .drvPath = makeConstantStorePathRef(drvPath),
                .outputs = OutputsSpec::Names{outputName},
            }},
            .program = program,
        }};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", cursor->getAttrPathStr(), type);
}

std::vector<BuiltPathWithResult> UnresolvedApp::build(ref<Store> evalStore, ref<Store> store)
{
    Installables installableContext;

    for (auto & ctxElt : unresolved.context)
        installableContext.push_back(make_ref<InstallableDerivedPath>(store, DerivedPath{ctxElt}));

    return Installable::build(evalStore, store, Realise::Outputs, installableContext);
}

// FIXME: move to libcmd
App UnresolvedApp::resolve(ref<Store> evalStore, ref<Store> store)
{
    auto res = unresolved;

    auto builtContext = build(evalStore, store);
    res.program = resolveString(*store, unresolved.program, builtContext);
    if (!store->isInStore(res.program))
        throw Error("app program '%s' is not in the Nix store", res.program);

    return res;
}

} // namespace nix
