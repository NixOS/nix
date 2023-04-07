#include "globals.hh"
#include "installable-attr-path.hh"
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

InstallableAttrPath::InstallableAttrPath(
    ref<EvalState> state,
    SourceExprCommand & cmd,
    Value * v,
    const std::string & attrPath,
    ExtendedOutputsSpec extendedOutputsSpec)
    : InstallableValue(state)
    , cmd(cmd)
    , v(allocRootValue(v))
    , attrPath(attrPath)
    , extendedOutputsSpec(std::move(extendedOutputsSpec))
{ }

std::pair<Value *, PosIdx> InstallableAttrPath::toValue(EvalState & state)
{
    auto [vRes, pos] = findAlongAttrPath(state, attrPath, *cmd.getAutoArgs(state), **v);
    state.forceValue(*vRes, pos);
    return {vRes, pos};
}

DerivedPathsWithInfo InstallableAttrPath::toDerivedPaths()
{
    auto v = toValue(*state).first;

    Bindings & autoArgs = *cmd.getAutoArgs(*state);

    DrvInfos drvInfos;
    getDerivations(*state, *v, "", autoArgs, drvInfos, false);

    // Backward compatibility hack: group results by drvPath. This
    // helps keep .all output together.
    std::map<StorePath, OutputsSpec> byDrvPath;

    for (auto & drvInfo : drvInfos) {
        auto drvPath = drvInfo.queryDrvPath();
        if (!drvPath)
            throw Error("'%s' is not a derivation", what());

        auto newOutputs = std::visit(overloaded {
            [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                std::set<std::string> outputsToInstall;
                for (auto & output : drvInfo.queryOutputs(false, true))
                    outputsToInstall.insert(output.first);
                return OutputsSpec::Names { std::move(outputsToInstall) };
            },
            [&](const ExtendedOutputsSpec::Explicit & e) -> OutputsSpec {
                return e;
            },
        }, extendedOutputsSpec.raw());

        auto [iter, didInsert] = byDrvPath.emplace(*drvPath, newOutputs);

        if (!didInsert)
            iter->second = iter->second.union_(newOutputs);
    }

    DerivedPathsWithInfo res;
    for (auto & [drvPath, outputs] : byDrvPath)
        res.push_back({
            .path = DerivedPath::Built {
                .drvPath = drvPath,
                .outputs = outputs,
            },
            .info = make_ref<ExtraPathInfoValue>(ExtraPathInfoValue::Value {
                /* FIXME: reconsider backwards compatibility above
                   so we can fill in this info. */
            }),
        });

    return res;
}

InstallableAttrPath InstallableAttrPath::parse(
    ref<EvalState> state,
    SourceExprCommand & cmd,
    Value * v,
    std::string_view prefix,
    ExtendedOutputsSpec extendedOutputsSpec)
{
    return {
        state, cmd, v,
        prefix == "." ? "" : std::string { prefix },
        extendedOutputsSpec
    };
}

}
