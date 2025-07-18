#include "nix/store/derived-path.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/util/comparator.hh"

#include <nlohmann/json.hpp>

#include <optional>

namespace nix {

// Custom implementation to avoid `ref` ptr equality
GENERATE_CMP_EXT(, std::strong_ordering, SingleDerivedPathBuilt, *me->drvPath, me->output);

// Custom implementation to avoid `ref` ptr equality

// TODO no `GENERATE_CMP_EXT` because no `std::set::operator<=>` on
// Darwin, per header.
GENERATE_EQUAL(, DerivedPathBuilt ::, DerivedPathBuilt, *me->drvPath, me->outputs);
GENERATE_ONE_CMP(, bool, DerivedPathBuilt ::, <, DerivedPathBuilt, *me->drvPath, me->outputs);

nlohmann::json DerivedPath::Opaque::toJSON(const StoreDirConfig & store) const
{
    return store.printStorePath(path);
}

nlohmann::json SingleDerivedPath::Built::toJSON(Store & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = store.queryPartialDerivationOutputMap(resolveDerivedPath(store, *drvPath));
    res["output"] = output;
    auto outputPathIter = outputMap.find(output);
    if (outputPathIter == outputMap.end())
        res["outputPath"] = nullptr;
    else if (std::optional p = outputPathIter->second)
        res["outputPath"] = store.printStorePath(*p);
    else
        res["outputPath"] = nullptr;
    return res;
}

nlohmann::json DerivedPath::Built::toJSON(Store & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = store.queryPartialDerivationOutputMap(resolveDerivedPath(store, *drvPath));
    for (const auto & [output, outputPathOpt] : outputMap) {
        if (!outputs.contains(output))
            continue;
        if (outputPathOpt)
            res["outputs"][output] = store.printStorePath(*outputPathOpt);
        else
            res["outputs"][output] = nullptr;
    }
    return res;
}

nlohmann::json SingleDerivedPath::toJSON(Store & store) const
{
    return std::visit([&](const auto & buildable) { return buildable.toJSON(store); }, raw());
}

nlohmann::json DerivedPath::toJSON(Store & store) const
{
    return std::visit([&](const auto & buildable) { return buildable.toJSON(store); }, raw());
}

std::string DerivedPath::Opaque::to_string(const StoreDirConfig & store) const
{
    return store.printStorePath(path);
}

std::string SingleDerivedPath::Built::to_string(const StoreDirConfig & store) const
{
    return drvPath->to_string(store) + "^" + output;
}

std::string SingleDerivedPath::Built::to_string_legacy(const StoreDirConfig & store) const
{
    return drvPath->to_string(store) + "!" + output;
}

std::string DerivedPath::Built::to_string(const StoreDirConfig & store) const
{
    return drvPath->to_string(store) + '^' + outputs.to_string();
}

std::string DerivedPath::Built::to_string_legacy(const StoreDirConfig & store) const
{
    return drvPath->to_string_legacy(store) + "!" + outputs.to_string();
}

std::string SingleDerivedPath::to_string(const StoreDirConfig & store) const
{
    return std::visit([&](const auto & req) { return req.to_string(store); }, raw());
}

std::string DerivedPath::to_string(const StoreDirConfig & store) const
{
    return std::visit([&](const auto & req) { return req.to_string(store); }, raw());
}

std::string SingleDerivedPath::to_string_legacy(const StoreDirConfig & store) const
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Built & req) { return req.to_string_legacy(store); },
            [&](const SingleDerivedPath::Opaque & req) { return req.to_string(store); },
        },
        this->raw());
}

std::string DerivedPath::to_string_legacy(const StoreDirConfig & store) const
{
    return std::visit(
        overloaded{
            [&](const DerivedPath::Built & req) { return req.to_string_legacy(store); },
            [&](const DerivedPath::Opaque & req) { return req.to_string(store); },
        },
        this->raw());
}

DerivedPath::Opaque DerivedPath::Opaque::parse(const StoreDirConfig & store, std::string_view s)
{
    return {store.parseStorePath(s)};
}

void drvRequireExperiment(const SingleDerivedPath & drv, const ExperimentalFeatureSettings & xpSettings)
{
    std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque &) {
                // plain drv path; no experimental features required.
            },
            [&](const SingleDerivedPath::Built &) { xpSettings.require(Xp::DynamicDerivations); },
        },
        drv.raw());
}

SingleDerivedPath::Built SingleDerivedPath::Built::parse(
    const StoreDirConfig & store,
    ref<const SingleDerivedPath> drv,
    OutputNameView output,
    const ExperimentalFeatureSettings & xpSettings)
{
    drvRequireExperiment(*drv, xpSettings);
    return {
        .drvPath = drv,
        .output = std::string{output},
    };
}

DerivedPath::Built DerivedPath::Built::parse(
    const StoreDirConfig & store,
    ref<const SingleDerivedPath> drv,
    OutputNameView outputsS,
    const ExperimentalFeatureSettings & xpSettings)
{
    drvRequireExperiment(*drv, xpSettings);
    return {
        .drvPath = drv,
        .outputs = OutputsSpec::parse(outputsS),
    };
}

static SingleDerivedPath parseWithSingle(
    const StoreDirConfig & store,
    std::string_view s,
    std::string_view separator,
    const ExperimentalFeatureSettings & xpSettings)
{
    size_t n = s.rfind(separator);
    return n == s.npos
               ? (SingleDerivedPath) SingleDerivedPath::Opaque::parse(store, s)
               : (SingleDerivedPath) SingleDerivedPath::Built::parse(
                     store,
                     make_ref<const SingleDerivedPath>(parseWithSingle(store, s.substr(0, n), separator, xpSettings)),
                     s.substr(n + 1),
                     xpSettings);
}

SingleDerivedPath SingleDerivedPath::parse(
    const StoreDirConfig & store, std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    return parseWithSingle(store, s, "^", xpSettings);
}

SingleDerivedPath SingleDerivedPath::parseLegacy(
    const StoreDirConfig & store, std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    return parseWithSingle(store, s, "!", xpSettings);
}

static DerivedPath parseWith(
    const StoreDirConfig & store,
    std::string_view s,
    std::string_view separator,
    const ExperimentalFeatureSettings & xpSettings)
{
    size_t n = s.rfind(separator);
    return n == s.npos
               ? (DerivedPath) DerivedPath::Opaque::parse(store, s)
               : (DerivedPath) DerivedPath::Built::parse(
                     store,
                     make_ref<const SingleDerivedPath>(parseWithSingle(store, s.substr(0, n), separator, xpSettings)),
                     s.substr(n + 1),
                     xpSettings);
}

DerivedPath
DerivedPath::parse(const StoreDirConfig & store, std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    return parseWith(store, s, "^", xpSettings);
}

DerivedPath DerivedPath::parseLegacy(
    const StoreDirConfig & store, std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    return parseWith(store, s, "!", xpSettings);
}

DerivedPath DerivedPath::fromSingle(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & o) -> DerivedPath { return o; },
            [&](const SingleDerivedPath::Built & b) -> DerivedPath {
                return DerivedPath::Built{
                    .drvPath = b.drvPath,
                    .outputs = OutputsSpec::Names{b.output},
                };
            },
        },
        req.raw());
}

const StorePath & SingleDerivedPath::Built::getBaseStorePath() const
{
    return drvPath->getBaseStorePath();
}

const StorePath & DerivedPath::Built::getBaseStorePath() const
{
    return drvPath->getBaseStorePath();
}

template<typename DP>
static inline const StorePath & getBaseStorePath_(const DP & derivedPath)
{
    return std::visit(
        overloaded{
            [&](const typename DP::Built & bfd) -> auto & { return bfd.drvPath->getBaseStorePath(); },
            [&](const typename DP::Opaque & bo) -> auto & { return bo.path; },
        },
        derivedPath.raw());
}

const StorePath & SingleDerivedPath::getBaseStorePath() const
{
    return getBaseStorePath_(*this);
}

const StorePath & DerivedPath::getBaseStorePath() const
{
    return getBaseStorePath_(*this);
}

} // namespace nix
