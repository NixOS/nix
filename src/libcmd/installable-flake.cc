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

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

std::vector<std::string> InstallableFlake::getActualAttrPaths()
{
    std::vector<std::string> res;

    for (auto & prefix : prefixes)
        res.push_back(prefix + *attrPaths.begin());

    for (auto & s : attrPaths)
        res.push_back(s);

    return res;
}

Value * InstallableFlake::getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake)
{
    auto vFlake = state.allocValue();

    callFlake(state, lockedFlake, *vFlake);

    auto aOutputs = vFlake->attrs->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceValue(*aOutputs->value, [&]() { return aOutputs->value->determinePos(noPos); });

    return aOutputs->value;
}

static std::string showAttrPaths(const std::vector<std::string> & paths)
{
    std::string s;
    for (const auto & [n, i] : enumerate(paths)) {
        if (n > 0) s += n + 1 == paths.size() ? " or " : ", ";
        s += '\''; s += i; s += '\'';
    }
    return s;
}

InstallableFlake::InstallableFlake(
    SourceExprCommand * cmd,
    ref<EvalState> state,
    FlakeRef && flakeRef,
    std::string_view fragment,
    ExtendedOutputsSpec extendedOutputsSpec,
    Strings attrPaths,
    Strings prefixes,
    const flake::LockFlags & lockFlags)
    : InstallableValue(state),
      flakeRef(flakeRef),
      attrPaths(fragment == "" ? attrPaths : Strings{(std::string) fragment}),
      prefixes(fragment == "" ? Strings{} : prefixes),
      extendedOutputsSpec(std::move(extendedOutputsSpec)),
      lockFlags(lockFlags)
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

        if (v.type() == nPath) {
            PathSet context;
            auto storePath = state->copyPathToStore(context, Path(v.path));
            return {{
                .path = DerivedPath::Opaque {
                    .path = std::move(storePath),
                },
                .info = make_ref<ExtraPathInfo>(),
            }};
        }

        else if (v.type() == nString) {
            PathSet context;
            auto s = state->forceString(v, context, noPos, fmt("while evaluating the flake output attribute '%s'", attrPath));
            auto storePath = state->store->maybeParseStorePath(s);
            if (storePath && context.count(std::string(s))) {
                return {{
                    .path = DerivedPath::Opaque {
                        .path = std::move(*storePath),
                    },
                    .info = make_ref<ExtraPathInfo>(),
                }};
            } else
                throw Error("flake output attribute '%s' evaluates to the string '%s' which is not a store path", attrPath, s);
        }

        else
            throw Error("flake output attribute '%s' is not a derivation or path", attrPath);
    }

    auto drvPath = attr->forceDerivation();

    std::optional<NixInt> priority;

    if (attr->maybeGetAttr(state->sOutputSpecified)) {
    } else if (auto aMeta = attr->maybeGetAttr(state->sMeta)) {
        if (auto aPriority = aMeta->maybeGetAttr("priority"))
            priority = aPriority->getInt();
    }

    return {{
        .path = DerivedPath::Built {
            .drvPath = std::move(drvPath),
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
            }, extendedOutputsSpec.raw()),
        },
        .info = make_ref<ExtraPathInfoFlake>(
            ExtraPathInfoValue::Value {
                .priority = priority,
                .attrPath = attrPath,
                .extendedOutputsSpec = extendedOutputsSpec,
            },
            ExtraPathInfoFlake::Flake {
                .originalRef = flakeRef,
                .resolvedRef = getLockedFlake()->flake.lockedRef,
            }),
    }};
}

std::pair<Value *, PosIdx> InstallableFlake::toValue(EvalState & state)
{
    return {&getCursor(state)->forceValue(), noPos};
}

std::vector<ref<eval_cache::AttrCursor>>
InstallableFlake::getCursors(EvalState & state)
{
    auto evalCache = openEvalCache(state, getLockedFlake());

    auto root = evalCache->getRoot();

    std::vector<ref<eval_cache::AttrCursor>> res;

    Suggestions suggestions;
    auto attrPaths = getActualAttrPaths();

    for (auto & attrPath : attrPaths) {
        debug("trying flake output attribute '%s'", attrPath);

        auto attr = root->findAlongAttrPath(parseAttrPath(state, attrPath));
        if (attr) {
            res.push_back(ref(*attr));
        } else {
            suggestions += attr.getSuggestions();
        }
    }

    if (res.size() == 0)
        throw Error(
            suggestions,
            "flake '%s' does not provide attribute %s",
            flakeRef,
            showAttrPaths(attrPaths));

    return res;
}

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake) {
        flake::LockFlags lockFlagsApplyConfig = lockFlags;
        // FIXME why this side effect?
        lockFlagsApplyConfig.applyNixConfig = true;
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(*state, flakeRef, lockFlagsApplyConfig));
    }
    return _lockedFlake;
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

    return InstallableValue::nixpkgsFlakeRef();
}

}
