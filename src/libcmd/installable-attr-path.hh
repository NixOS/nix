#pragma once
///@file

#include "globals.hh"
#include "installable-value.hh"
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
#include "eval-cache.hh"
#include "url.hh"
#include "registry.hh"
#include "build-result.hh"

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
