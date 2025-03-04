#include "globals.hh"
#include "installable-flake.hh"
#include "installable-derived-path.hh"
#include "outputs-spec.hh"
#include "util.hh"
#include "command.hh"
#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"
#include "flake/flake.hh"
#include "eval-cache.hh"
#include "url.hh"
#include "registry.hh"
#include "build-result.hh"
#include "flake-schemas.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

static std::string showAttrPaths(
    EvalState & state,
    const std::vector<eval_cache::AttrPath> & paths)
{
    std::string s;
    for (const auto & [n, i] : enumerate(paths)) {
        if (n > 0) s += n + 1 == paths.size() ? " or " : ", ";
        s += '\''; s += eval_cache::toAttrPathStr(state, i); s += '\'';
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
    , isAbsolute(fragment.starts_with("."))
    , relativeFragment(isAbsolute ? fragment.substr(1) : fragment)
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

    if (!attr->isDerivation()) {

        // FIXME: use eval cache?
        auto v = attr->forceValue();

        if (std::optional derivedPathWithInfo = trySinglePathToDerivedPaths(
            v,
            noPos,
            fmt("while evaluating the flake output attribute '%s'", attrPath)))
        {
            return { *derivedPathWithInfo };
        } else {
            throw Error(
                "expected flake output attribute '%s' to be a derivation or path but found %s: %s",
                attrPath,
                showType(v),
                ValuePrinter(*this->state, v, errorPrintOptions)
            );
        }
    }

    auto drvPath = attr->forceDerivation();

    std::optional<NixInt::Inner> priority;

    if (attr->maybeGetAttr(state->sOutputSpecified)) {
    } else if (auto aMeta = attr->maybeGetAttr(state->sMeta)) {
        if (auto aPriority = aMeta->maybeGetAttr("priority"))
            priority = aPriority->getInt().value;
    }

    return {{
        .path = DerivedPath::Built {
            .drvPath = makeConstantStorePathRef(std::move(drvPath)),
            .outputs = std::visit(overloaded {
                [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                    std::set<std::string> outputsToInstall;
                    if (auto aOutputSpecified = attr->maybeGetAttr(state->sOutputSpecified)) {
                        if (aOutputSpecified->getBool()) {
                            if (auto aOutputName = attr->maybeGetAttr("outputName"))
                                outputsToInstall = { aOutputName->getString() };
                        }
                    } else if (auto aMeta = attr->maybeGetAttr(state->sMeta)) {
                        if (auto aOutputsToInstall = aMeta->maybeGetAttr("outputsToInstall"))
                            for (auto & s : aOutputsToInstall->getListOfStrings())
                                outputsToInstall.insert(s);
                    }

                    if (outputsToInstall.empty())
                        outputsToInstall.insert("out");

                    return OutputsSpec::Names { std::move(outputsToInstall) };
                },
                [&](const ExtendedOutputsSpec::Explicit & e) -> OutputsSpec {
                    return e;
                },
            }, extendedOutputsSpec.raw),
        },
        .info = make_ref<ExtraPathInfoFlake>(
            ExtraPathInfoValue::Value {
                .priority = priority,
                .attrPath = attrPath,
                .extendedOutputsSpec = extendedOutputsSpec,
            },
            ExtraPathInfoFlake::Flake {
                .originalRef = flakeRef,
                .lockedRef = getLockedFlake()->flake.lockedRef,
            }),
    }};
}

std::pair<Value *, PosIdx> InstallableFlake::toValue(EvalState & state)
{
    return {&getCursor(state)->forceValue(), noPos};
}

std::vector<ref<eval_cache::AttrCursor>>
InstallableFlake::getCursors(EvalState & state, bool returnAll)
{
    auto cache = openEvalCache();

    auto inventory = cache->getRoot()->getAttr("inventory");

    std::vector<ref<eval_cache::AttrCursor>> res;

    Suggestions suggestions;

    auto attrPaths = getAttrPaths(state, relativeFragment);

    if (attrPaths.empty())
        throw Error("flake '%s' does not provide a default output", flakeRef);

    for (auto & attrPath : attrPaths) {
        debug("trying flake output attribute '%s'", eval_cache::toAttrPathStr(state, attrPath));

        if (attrPath.empty()) {
            res.push_back(cache->getRoot()->getAttr("outputs"));
            if (!returnAll) break;
        }

        else {
            auto outputInfo = flake_schemas::getOutput(inventory, attrPath);

            if (!outputInfo) {
                suggestions += outputInfo.getSuggestions();
                continue;
            }

            if (outputInfo->leafAttrPath.empty()) {
                if (auto drv = outputInfo->nodeInfo->maybeGetAttr("derivation")) {
                    res.push_back(ref(drv));
                    continue;
                }
            }

            auto attr = outputInfo->rawValue->findAlongAttrPath(outputInfo->leafAttrPath);
            if (attr) {
                res.push_back(ref(*attr));
                if (!returnAll) break;
            } else
                suggestions += attr.getSuggestions();
        }
    }

    if (res.size() == 0)
        throw Error(
            suggestions,
            "flake '%s' does not provide attribute %s",
            flakeRef,
            showAttrPaths(state, attrPaths));

    return res;
}

void InstallableFlake::getCompletions(AddCompletions & completions, std::string_view prefix) const
{
    auto cache = openEvalCache();

    auto inventory = cache->getRoot()->getAttr("inventory");
    auto outputs = cache->getRoot()->getAttr("outputs");

    auto attrPaths = getAttrPaths(*state, relativeFragment, true);

    if (fragment.empty())
        attrPaths.push_back({});

    for (auto & attrPath : attrPaths) {
        std::string lastAttr;
        if (!attrPath.empty() && !hasSuffix(relativeFragment, ".")) {
            lastAttr = state->symbols[attrPath.back()];
            attrPath.pop_back();
        }

        auto search = [&](ref<eval_cache::AttrCursor> cursor)
        {
            for (auto & attr : cursor->getAttrs())
                if (hasPrefix(state->symbols[attr], lastAttr))
                    completions.add(std::string(prefix) + state->symbols[attr]);
        };

        if (attrPath.empty())
            search(outputs);
        else if (auto outputInfo = flake_schemas::getOutput(inventory, attrPath))
            search(outputInfo->rawValue);
    }
}

std::vector<eval_cache::AttrPath> InstallableFlake::getAttrPaths(EvalState & state, std::string_view attrPathS, bool forCompletion) const
{
    std::vector<eval_cache::AttrPath> attrPaths;

    auto parsedFragment = parseAttrPath(state, attrPathS);

    if (isAbsolute)
        attrPaths.push_back(parsedFragment);
    else {
        auto schemas = flake_schemas::getSchema(openEvalCache()->getRoot()->getAttr("inventory"));

        // FIXME: Ugly hack to preserve the historical precedence
        // between outputs. We should add a way for schemas to declare
        // priorities.
        std::vector<std::string> schemasSorted;
        std::set<std::string> schemasSeen;
        auto doSchema = [&](const std::string & schema)
        {
            if (schemas.contains(schema)) {
                schemasSorted.push_back(schema);
                schemasSeen.insert(schema);
            }
        };
        doSchema("apps");
        doSchema("devShells");
        doSchema("packages");
        doSchema("legacyPackages");
        for (auto & schema : schemas)
            if (!schemasSeen.contains(schema.first))
                schemasSorted.push_back(schema.first);

        for (auto & role : roles) {
            for (auto & schemaName : schemasSorted) {
                auto & schema = schemas.find(schemaName)->second;
                if (schema.roles.contains(role)) {
                    eval_cache::AttrPath attrPath{state.symbols.create(schemaName)};
                    if (schema.appendSystem)
                        attrPath.push_back(state.symbols.create(settings.thisSystem.get()));

                    if (parsedFragment.empty()) {
                        if (schema.defaultAttrPath) {
                            auto attrPath2{attrPath};
                            for (auto & x : *schema.defaultAttrPath)
                                attrPath2.push_back(x);
                            attrPaths.push_back(attrPath2);
                        }
                    }

                    if (!parsedFragment.empty() || forCompletion) {
                        auto attrPath2{attrPath};
                        if (parsedFragment.empty())
                            // Add dummy symbol that will be dropped
                            // by getCompletions().
                            attrPath2.push_back(state.symbols.create(""));
                        else
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
    }

    return attrPaths;
}

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake) {
        flake::LockFlags lockFlagsApplyConfig = lockFlags;
        // FIXME why this side effect?
        lockFlagsApplyConfig.applyNixConfig = true;
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(
            flakeSettings, *state, flakeRef, lockFlagsApplyConfig));
    }
    return _lockedFlake;
}

ref<eval_cache::EvalCache> InstallableFlake::openEvalCache() const
{
    if (!_evalCache) {
        _evalCache = flake_schemas::call(
            *state,
            getLockedFlake(),
            defaultFlakeSchemas);
    }
    return ref(_evalCache);
}


FlakeRef InstallableFlake::nixpkgsFlakeRef() const
{
    auto lockedFlake = getLockedFlake();

    if (auto nixpkgsInput = lockedFlake->lockFile.findInput({"nixpkgs"})) {
        if (auto lockedNode = std::dynamic_pointer_cast<const flake::LockedNode>(nixpkgsInput)) {
            debug("using nixpkgs flake '%s'", lockedNode->lockedRef);
            return std::move(lockedNode->lockedRef);
        }
    }

    return defaultNixpkgsFlakeRef();
}

}
