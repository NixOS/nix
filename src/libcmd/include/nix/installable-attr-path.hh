#pragma once
///@file

#include "nix/globals.hh"
#include "nix/installable-value.hh"
#include "nix/outputs-spec.hh"
#include "nix/command.hh"
#include "nix/attr-path.hh"
#include "nix/common-eval-args.hh"
#include "nix/derivations.hh"
#include "nix/eval-inline.hh"
#include "nix/eval.hh"
#include "nix/get-drvs.hh"
#include "nix/store-api.hh"
#include "nix/shared.hh"
#include "nix/eval-cache.hh"
#include "nix/url.hh"
#include "nix/registry.hh"
#include "nix/build-result.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

class InstallableAttrPath : public InstallableValue
{
    SourceExprCommand & cmd;
    RootValue v;
    std::string attrPath;
    ExtendedOutputsSpec extendedOutputsSpec;

    InstallableAttrPath(
        ref<EvalState> state,
        SourceExprCommand & cmd,
        Value * v,
        const std::string & attrPath,
        ExtendedOutputsSpec extendedOutputsSpec);

    std::string what() const override { return attrPath; };

    std::pair<Value *, PosIdx> toValue(EvalState & state) override;

    DerivedPathsWithInfo toDerivedPaths() override;

public:

    static InstallableAttrPath parse(
        ref<EvalState> state,
        SourceExprCommand & cmd,
        Value * v,
        std::string_view prefix,
        ExtendedOutputsSpec extendedOutputsSpec);
};

}
