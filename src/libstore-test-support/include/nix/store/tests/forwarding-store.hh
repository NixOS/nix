#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/util/callback.hh"

namespace nix {

/**
 * A store that hands every operation to another store. A test derives
 * from it and overrides the few operations it wants to observe or
 * replace, while the rest keep working.
 */
struct ForwardingStore : Store
{
    ref<Store> inner;

    ForwardingStore(ref<Store> inner)
        : Store{inner->config}
        , inner(inner)
    {
    }

    void anchor() override {}

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return inner->isTrustedClient();
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        return inner->queryPathFromHashPart(hashPart);
    }

    void addToStore(const ValidPathInfo & info, Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs) override
    {
        inner->addToStore(info, narSource, repair, checkSigs);
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
        return inner->addToStoreFromDump(dump, name, dumpMethod, hashMethod, hashAlgo, references, repair);
    }

    StorePath writeDerivation(const Derivation & drv, RepairFlag repair) override
    {
        return inner->writeDerivation(drv, repair);
    }

    Derivation readDerivation(const StorePath & drvPath) override
    {
        return inner->readDerivation(drvPath);
    }

    Derivation readInvalidDerivation(const StorePath & drvPath) override
    {
        return inner->readInvalidDerivation(drvPath);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return inner->getFSAccessor(requireValidPath);
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath) override
    {
        return inner->getFSAccessor(path, requireValidPath);
    }

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute) override
    {
        return inner->queryValidPaths(paths, maybeSubstitute);
    }

    void addTempRoots(const StorePathSet & paths) override
    {
        inner->addTempRoots(paths);
    }

protected:

    bool isValidPathUncached(const StorePath & path) override
    {
        return inner->isValidPath(path);
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        try {
            callback(inner->queryPathInfo(path).get_ptr());
        } catch (InvalidPath &) {
            callback(nullptr);
        } catch (...) {
            callback.rethrow();
        }
    }

    void queryRealisationUncached(
        const DrvOutput & drvOutput, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override
    {
        try {
            callback(inner->queryRealisation(drvOutput));
        } catch (...) {
            callback.rethrow();
        }
    }

    void registerDrvOutputUnchecked(const Realisation & output) override
    {
        inner->registerDrvOutput(output, NoCheckSigs);
    }
};

} // namespace nix
