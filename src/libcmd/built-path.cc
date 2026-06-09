#include "nix/cmd/built-path.hh"
#include "nix/store/build-result.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/outputs-query.hh"
#include "nix/util/comparator.hh"

#include <nlohmann/json.hpp>

namespace nix {

// Custom implementation to avoid `ref` ptr equality
GENERATE_CMP_EXT(, std::strong_ordering, SingleBuiltPathBuilt, *me->drvPath, me->output);

// Custom implementation to avoid `ref` ptr equality

// TODO no `GENERATE_CMP_EXT` because no `std::set::operator<=>` on
// Darwin, per header.
GENERATE_EQUAL(, BuiltPathBuilt ::, BuiltPathBuilt, *me->drvPath, me->outputs);

StorePath SingleBuiltPath::outPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) { return p.path; },
            [](const SingleBuiltPath::Built & b) { return b.output.second; },
        },
        raw());
}

StorePathSet BuiltPath::outPaths() const
{
    return std::visit(
        overloaded{
            [](const BuiltPath::Opaque & p) { return StorePathSet{p.path}; },
            [](const BuiltPath::Built & b) {
                StorePathSet res;
                for (auto & [_, path] : b.outputs)
                    res.insert(path);
                return res;
            },
        },
        raw());
}

SingleDerivedPath::Built SingleBuiltPath::Built::discardOutputPath() const
{
    return SingleDerivedPath::Built{
        .drvPath = make_ref<SingleDerivedPath>(drvPath->discardOutputPath()),
        .output = output.first,
    };
}

SingleDerivedPath SingleBuiltPath::discardOutputPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) -> SingleDerivedPath { return p; },
            [](const SingleBuiltPath::Built & b) -> SingleDerivedPath { return b.discardOutputPath(); },
        },
        raw());
}

nlohmann::json BuiltPath::Built::toJSON(const StoreDirConfig & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    for (const auto & [outputName, outputPath] : outputs) {
        res["outputs"][outputName] = store.printStorePath(outputPath);
    }
    return res;
}

nlohmann::json SingleBuiltPath::Built::toJSON(const StoreDirConfig & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    auto & [outputName, outputPath] = output;
    res["output"] = outputName;
    res["outputPath"] = store.printStorePath(outputPath);
    return res;
}

nlohmann::json SingleBuiltPath::toJSON(const StoreDirConfig & store) const
{
    return std::visit(
        overloaded{
            [&](const SingleBuiltPath::Opaque & o) -> nlohmann::json { return store.printStorePath(o.path); },
            [&](const SingleBuiltPath::Built & b) { return b.toJSON(store); },
        },
        raw());
}

nlohmann::json BuiltPath::toJSON(const StoreDirConfig & store) const
{
    return std::visit(
        overloaded{
            [&](const BuiltPath::Opaque & o) -> nlohmann::json { return store.printStorePath(o.path); },
            [&](const BuiltPath::Built & b) { return b.toJSON(store); },
        },
        raw());
}

RealisedPath::Set BuiltPath::toRealisedPaths(Store & store) const
{
    RealisedPath::Set res;
    std::visit(
        overloaded{
            [&](const BuiltPath::Opaque & p) { res.insert(p.path); },
            [&](const BuiltPath::Built & p) {
                for (auto & [outputName, outputPath] : p.outputs) {
                    /* Use a custom callback to collect realisations as they're queried. */
                    deepQueryPartialDerivationOutput(
                        store, p.drvPath->outPath(), outputName, nullptr, [&](const DrvOutput & drvOutput) {
                            auto realisation = store.queryRealisation(drvOutput);
                            if (realisation)
                                res.insert(Realisation{*realisation, drvOutput});
                            return realisation;
                        });
                    res.insert(outputPath);
                }
            },
        },
        raw());
    return res;
}

SingleBuiltPath getBuiltPath(ref<Store> evalStore, ref<Store> store, const SingleDerivedPath & b)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) -> SingleBuiltPath { return SingleBuiltPath::Opaque{bo.path}; },
            [&](const SingleDerivedPath::Built & bfd) -> SingleBuiltPath {
                auto drvPath = getBuiltPath(evalStore, store, *bfd.drvPath);
                // Resolving this instead of `bfd` will yield the same result, but avoid duplicative work.
                SingleDerivedPath::Built truncatedBfd{
                    .drvPath = makeConstantStorePathRef(drvPath.outPath()),
                    .output = bfd.output,
                };
                auto outputPath = resolveDerivedPath(*store, truncatedBfd, &*evalStore);
                return SingleBuiltPath::Built{
                    .drvPath = make_ref<SingleBuiltPath>(std::move(drvPath)),
                    .output = {bfd.output, outputPath},
                };
            },
        },
        b.raw());
}

BuiltPath toBuiltPath(KeyedBuildResult & result, ref<Store> evalStore, ref<Store> store)
{
    auto success = result.tryGetSuccess();
    assert(success);
    return std::visit(
        overloaded{
            [&](const DerivedPath::Built & bfd) {
                std::map<std::string, StorePath> outputs;
                for (auto & [outputName, realisation] : success->builtOutputs)
                    outputs.emplace(outputName, realisation.outPath);
                BuiltPath bp = BuiltPath::Built{
                    .drvPath = make_ref<SingleBuiltPath>(getBuiltPath(evalStore, store, *bfd.drvPath)),
                    .outputs = outputs,
                };
                return bp;
            },
            [&](const DerivedPath::Opaque & bo) {
                BuiltPath bp = BuiltPath::Opaque{bo.path};
                return bp;
            },
        },
        result.path.raw());
}

} // namespace nix
