#include "nix/cmd/built-path.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/util/comparator.hh"
#include "nix/util/callback.hh"

#include <nlohmann/json.hpp>

#include <optional>

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

/**
 * A wrapper store that collects all queried realisations into a set.
 * Used by toRealisedPaths to gather realisations for copying.
 */
struct RealisationCollectingStore : Store
{
    Store & wrapped;
    RealisedPath::Set & collected;

    RealisationCollectingStore(Store & wrapped, RealisedPath::Set & collected)
        : Store(wrapped.config)
        , wrapped(wrapped)
        , collected(collected)
    {
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        try {
            callback(wrapped.queryPathInfo(path));
        } catch (...) {
            callback.rethrow();
        }
    }

    void queryRealisationUncached(
        const DrvOutput & drvOutput, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override
    {
        auto realisation = wrapped.queryRealisation(drvOutput);
        if (realisation) {
            collected.insert(Realisation{*realisation, drvOutput});
        }
        callback(std::move(realisation));
    }

    bool isValidPathUncached(const StorePath & path) override
    {
        return wrapped.isValidPath(path);
    }

    StorePathSet queryAllValidPaths() override
    {
        return wrapped.queryAllValidPaths();
    }

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override
    {
        wrapped.queryReferrers(path, referrers);
    }

    StorePathSet queryValidDerivers(const StorePath & path) override
    {
        return wrapped.queryValidDerivers(path);
    }

    std::map<std::string, std::optional<StorePath>>
    queryStaticPartialDerivationOutputMap(const StorePath & path) override
    {
        return wrapped.queryStaticPartialDerivationOutputMap(path);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        return wrapped.queryPathFromHashPart(hashPart);
    }

    void addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override
    {
        wrapped.addToStore(info, source, repair, checkSigs);
    }

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override
    {
        return wrapped.addToStoreFromDump(dump, name, dumpMethod, hashMethod, hashAlgo, references, repair);
    }

    void registerDrvOutput(const Realisation & info) override
    {
        wrapped.registerDrvOutput(info);
    }

    void registerDrvOutput(const Realisation & info, CheckSigsFlag checkSigs) override
    {
        wrapped.registerDrvOutput(info, checkSigs);
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        wrapped.narFromPath(path, sink);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return wrapped.getFSAccessor(requireValidPath);
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath) override
    {
        return wrapped.getFSAccessor(path, requireValidPath);
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return wrapped.isTrustedClient();
    }
};

RealisedPath::Set BuiltPath::toRealisedPaths(Store & store) const
{
    RealisedPath::Set res;
    RealisationCollectingStore collectingStore(store, res);
    std::visit(
        overloaded{
            [&](const BuiltPath::Opaque & p) { res.insert(p.path); },
            [&](const BuiltPath::Built & p) {
                for (auto & [outputName, outputPath] : p.outputs) {
                    /* Call deepQueryPartialDerivationOutput to trigger
                       realisation collection via the wrapper store. */
                    collectingStore.deepQueryPartialDerivationOutput(p.drvPath->outPath(), outputName);
                    res.insert(outputPath);
                }
            },
        },
        raw());
    return res;
}

} // namespace nix
