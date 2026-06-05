#include "nix/store/globals.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/cmd/installable-derived-path.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/util.hh"
#include "nix/cmd/command.hh"
#include "nix/expr/attr-path.hh"
#include "nix/cmd/common-eval-args.hh"
#include "nix/store/derivations.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/store/store-api.hh"
#include "nix/main/shared.hh"
#include "nix/flake/flake.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/util/url.hh"
#include "nix/fetchers/registry.hh"
#include "nix/store/build-result.hh"
#include "nix/flake/provenance.hh"
#include "nix/cmd/flake-schemas.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

static std::string showAttrPaths(EvalState & state, const std::vector<AttrPath> & paths)
{
    std::string s;
    for (const auto & [n, i] : enumerate(paths)) {
        if (n > 0)
            s += n + 1 == paths.size() ? " or " : ", ";
        s += '\'';
        s += i.to_string(state);
        s += '\'';
    }
    return s;
}

InstallableFlake::InstallableFlake(
    SourceExprCommand * cmd,
    ref<EvalState> state,
    FlakeRef && flakeRef,
    std::string_view fragment,
    ExtendedOutputsSpec extendedOutputsSpec,
    StringSet roles,
    const flake::LockFlags & lockFlags,
    std::optional<FlakeRef> defaultFlakeSchemas)
    : InstallableValue(state)
    , flakeRef(flakeRef)
    , fragment(fragment)
    , parsedFragment(AttrPath::parse(*state, fragment))
    , roles(roles)
    , extendedOutputsSpec(std::move(extendedOutputsSpec))
    , lockFlags(lockFlags)
    , defaultFlakeSchemas(defaultFlakeSchemas)
{
    if (cmd && cmd->getAutoArgs(*state)->size())
        throw UsageError("'--arg' and '--argstr' are incompatible with flakes");
}

DerivedPathsWithInfo InstallableFlake::toDerivedPaths()
{
    Activity act(*logger, lvlTalkative, actUnknown, fmt("evaluating derivation '%s'", what()));

    auto attr = getCursor(*state);

    auto attrPath = attr->getAttrPathStr();

    PushProvenance pushedProvenance(*state, makeProvenance(attrPath));

    if (!attr->isDerivation()) {

        // FIXME: use eval cache?
        auto v = attr->forceValue();

        if (std::optional derivedPathWithInfo = trySinglePathToDerivedPaths(
                v, noPos, fmt("while evaluating the flake output attribute '%s'", attrPath))) {
            return {*derivedPathWithInfo};
        } else {
            throw Error(
                "expected flake output attribute '%s' to be a derivation or path but found %s: %s",
                attrPath,
                showType(v),
                ValuePrinter(*this->state, v, errorPrintOptions));
        }
    }

    auto drvPath = attr->forceDerivation();
    state->waitForPath(drvPath);

    std::optional<NixInt::Inner> priority;

    if (attr->maybeGetAttr(state->s.outputSpecified)) {
    } else if (auto aMeta = attr->maybeGetAttr(state->s.meta)) {
        if (auto aPriority = aMeta->maybeGetAttr("priority"))
            priority = aPriority->getInt().value;
    }

    return {{
        .path =
            DerivedPath::Built{
                .drvPath = makeConstantStorePathRef(std::move(drvPath)),
                .outputs = std::visit(
                    overloaded{
                        [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                            StringSet outputsToInstall;
                            if (auto aOutputSpecified = attr->maybeGetAttr(state->s.outputSpecified)) {
                                if (aOutputSpecified->getBool()) {
                                    if (auto aOutputName = attr->maybeGetAttr("outputName"))
                                        outputsToInstall = {aOutputName->getString()};
                                }
                            } else if (auto aMeta = attr->maybeGetAttr(state->s.meta)) {
                                if (auto aOutputsToInstall = aMeta->maybeGetAttr("outputsToInstall"))
                                    for (auto & s : aOutputsToInstall->getListOfStrings())
                                        outputsToInstall.insert(s);
                            }

                            if (outputsToInstall.empty())
                                outputsToInstall.insert("out");

                            return OutputsSpec::Names{std::move(outputsToInstall)};
                        },
                        [&](const ExtendedOutputsSpec::Explicit & e) -> OutputsSpec { return e; },
                    },
                    extendedOutputsSpec.raw),
            },
        .info = make_ref<ExtraPathInfoFlake>(
            ExtraPathInfoValue::Value{
                .priority = priority,
                .attrPath = attrPath,
                .extendedOutputsSpec = extendedOutputsSpec,
            },
            ExtraPathInfoFlake::Flake{
                .originalRef = flakeRef,
                .lockedRef = getLockedFlake()->flake.lockedRef,
            }),
    }};
}

std::pair<Value *, PosIdx> InstallableFlake::toValue(EvalState & state)
{
    return {&getCursor(state)->forceValue(), noPos};
}

std::vector<AttrPath> InstallableFlake::getAttrPaths(bool useDefaultAttrPath, ref<eval_cache::AttrCursor> inventory)
{
    if (fragment.starts_with("."))
        return {AttrPath::parse(*state, fragment.substr(1))};

    std::vector<AttrPath> attrPaths;

    auto schemas = flake_schemas::getSchemas(inventory);

    // FIXME: Ugly hack to preserve the historical precedence
    // between outputs. We should add a way for schemas to declare
    // priorities.
    std::vector<std::string> schemasSorted;
    std::set<std::string> schemasSeen;
    auto doSchema = [&](const std::string & schema) {
        if (schemas.contains(schema)) {
            schemasSorted.push_back(schema);
            schemasSeen.insert(schema);
        }
    };
    doSchema("apps");
    doSchema("defaultApp");
    doSchema("devShells");
    doSchema("devShell");
    doSchema("packages");
    doSchema("defaultPackage");
    doSchema("legacyPackages");
    for (auto & schema : schemas)
        if (!schemasSeen.contains(schema.first))
            schemasSorted.push_back(schema.first);

    for (auto & role : roles) {
        for (auto & schemaName : schemasSorted) {
            auto & schema = schemas.find(schemaName)->second;
            if (schema.roles.contains(role)) {
                AttrPath attrPath{state->symbols.create(schemaName)};
                if (schema.appendSystem)
                    attrPath.push_back(state->symbols.create(settings.thisSystem.get()));

                if (useDefaultAttrPath && parsedFragment.empty()) {
                    if (schema.defaultAttrPath) {
                        auto attrPath2{attrPath};
                        for (auto & x : *schema.defaultAttrPath)
                            attrPath2.push_back(x);
                        attrPaths.push_back(attrPath2);
                    }
                } else {
                    auto attrPath2{attrPath};
                    for (auto & x : parsedFragment)
                        attrPath2.push_back(x);
                    attrPaths.push_back(attrPath2);
                }
            }
        }
    }

    if (!parsedFragment.empty())
        attrPaths.push_back(parsedFragment);

    // FIXME: compatibility hack to get `nix repl` to return all
    // outputs by default.
    if (parsedFragment.empty() && roles.contains("nix-repl"))
        attrPaths.push_back({});

    return attrPaths;
}

std::vector<ref<eval_cache::AttrCursor>> InstallableFlake::getCursors(EvalState & state, bool useDefaultAttrPath)
{
    auto cache = openEvalCache();

    auto inventory = cache->getRoot()->getAttr("inventory");
    auto outputs = cache->getRoot()->getAttr("outputs");

    auto attrPaths = getAttrPaths(useDefaultAttrPath, inventory);

    if (attrPaths.empty())
        throw Error(
            "Flake '%s' does not have any schema that provides a default output for the role(s) %s.",
            flakeRef,
            concatStringsSep(", ", roles));

    std::vector<ref<eval_cache::AttrCursor>> res;

    Suggestions suggestions;

    for (auto & attrPath : attrPaths) {
        debug("trying flake output attribute '%s'", attrPath.to_string(state));

        PushProvenance pushedProvenance(state, makeProvenance(attrPath.to_string(state)));

#if 0
        auto outputInfo = flake_schemas::getOutputInfo(inventory, attrPath);

        if (outputInfo && outputInfo->leafAttrPath.empty()) {
            if (auto drv = outputInfo->nodeInfo->maybeGetAttr("derivation")) {
                res.push_back(ref(drv));
                continue;
            }
        }
#endif

        try {
            auto attr = outputs->findAlongAttrPath(attrPath);
            if (attr)
                res.push_back(ref(*attr));
            else
                suggestions += attr.getSuggestions();
        } catch (TypeError & e) {
            debug("error resolving attribute '%s': %s", attrPath.to_string(state), e.msg());
            // Continue to next attribute path
        }
    }

    if (res.size() == 0)
        throw Error(suggestions, "flake '%s' does not provide attribute %s", flakeRef, showAttrPaths(state, attrPaths));

    return res;
}

void InstallableFlake::getCompletions(const std::string & flakeRefS, AddCompletions & completions)
{
    auto cache = openEvalCache();

    auto inventory = cache->getRoot()->getAttr("inventory");
    auto outputs = cache->getRoot()->getAttr("outputs");

    if (fragment.ends_with(".") || fragment.empty())
        // Represent that we're looking for attributes starting with the empty prefix (i.e. all attributes inside the
        // parent.
        parsedFragment.push_back(state->symbols.create(""));

    auto attrPaths = getAttrPaths(true, inventory);

    if (fragment.empty())
        // Return all top-level flake outputs.
        attrPaths.push_back(AttrPath{state->symbols.create("")});

    auto lastAttr = fragment.ends_with(".") || parsedFragment.empty() ? std::string_view("")
                                                                      : state->symbols[parsedFragment.back()];
    std::string prefix;
    if (auto dot = fragment.rfind('.'); dot != std::string::npos)
        prefix = fragment.substr(0, dot);
    if (fragment.starts_with(".") && !prefix.starts_with("."))
        prefix = "." + prefix;

    for (auto attrPath : attrPaths) {
        if (attrPath.empty())
            attrPath.push_back(state->symbols.create(""));

        auto attrPathParent{attrPath};
        attrPathParent.pop_back();

        auto attr = outputs->findAlongAttrPath(attrPathParent);
        if (!attr)
            continue;

        for (auto & childName : (*attr)->getAttrs()) {
            if (hasPrefix(state->symbols[childName], lastAttr)) {
                auto attrPathChild = (*attr)->getAttrPath(childName);
                completions.add(
                    flakeRefS + "#" + prefix + (prefix.empty() || prefix.ends_with(".") ? "" : ".")
                    + state->symbols[childName]);
            }
        }
    }
}

ref<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake) {
        flake::LockFlags lockFlagsApplyConfig = lockFlags;
        // FIXME why this side effect?
        lockFlagsApplyConfig.applyNixConfig = true;
        _lockedFlake = make_ref<flake::LockedFlake>(lockFlake(flakeSettings, *state, flakeRef, lockFlagsApplyConfig));
    }
    // _lockedFlake is now non-null but still just a shared_ptr
    return ref<flake::LockedFlake>(_lockedFlake);
}

ref<eval_cache::EvalCache> InstallableFlake::openEvalCache() const
{
    if (!_evalCache) {
        _evalCache = flake_schemas::call(*state, getLockedFlake(), defaultFlakeSchemas, useEvalCache);
    }
    return ref(_evalCache);
}

FlakeRef InstallableFlake::nixpkgsFlakeRef() const
{
    auto lockedFlake = getLockedFlake();

    if (auto nixpkgsInput = lockedFlake->lockFile.findInput({"nixpkgs"})) {
        if (auto lockedNode = std::dynamic_pointer_cast<const flake::LockedNode>(nixpkgsInput)) {
            if (lockedNode->isFlake) {
                debug("using nixpkgs flake '%s'", lockedNode->lockedRef);
                return std::move(lockedNode->lockedRef);
            }
        }
    }

    return defaultNixpkgsFlakeRef();
}

std::shared_ptr<const Provenance> InstallableFlake::makeProvenance(std::string_view attrPath) const
{
    auto provenance = getLockedFlake()->flake.provenance;
    if (!provenance)
        return nullptr;
    return std::make_shared<const FlakeProvenance>(provenance, std::string(attrPath), evalSettings.pureEval);
}

} // namespace nix
