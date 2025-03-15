#include "restricted-store.hh"
#include "callback.hh"
#include "realisation.hh"

namespace nix {

StorePathSet RestrictedStore::queryAllValidPaths()
{
    StorePathSet paths;
    for (auto & p : goal.originalPaths())
        paths.insert(p);
    for (auto & p : goal.addedPaths)
        paths.insert(p);
    return paths;
}

void RestrictedStore::queryPathInfoUncached(
    const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    if (goal.isAllowed(path)) {
        try {
            /* Censor impure information. */
            auto info = std::make_shared<ValidPathInfo>(*next->queryPathInfo(path));
            info->deriver.reset();
            info->registrationTime = 0;
            info->ultimate = false;
            info->sigs.clear();
            callback(info);
        } catch (InvalidPath &) {
            callback(nullptr);
        }
    } else
        callback(nullptr);
};

void RestrictedStore::queryReferrers(const StorePath & path, StorePathSet & referrers) {}

std::map<std::string, std::optional<StorePath>>
RestrictedStore::queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore)
{
    if (!goal.isAllowed(path))
        throw InvalidPath("cannot query output map for unknown path '%s' in recursive Nix", printStorePath(path));
    return next->queryPartialDerivationOutputMap(path, evalStore);
}

void RestrictedStore::addToStore(
    const ValidPathInfo & info, Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs)
{
    next->addToStore(info, narSource, repair, checkSigs);
    goal.addDependency(info.path);
}

StorePath RestrictedStore::addToStoreFromDump(
    Source & dump,
    std::string_view name,
    FileSerialisationMethod dumpMethod,
    ContentAddressMethod hashMethod,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    RepairFlag repair)
{
    auto path = next->addToStoreFromDump(dump, name, dumpMethod, hashMethod, hashAlgo, references, repair);
    goal.addDependency(path);
    return path;
}

void RestrictedStore::narFromPath(const StorePath & path, Sink & sink)
{
    if (!goal.isAllowed(path))
        throw InvalidPath("cannot dump unknown path '%s' in recursive Nix", printStorePath(path));
    LocalFSStore::narFromPath(path, sink);
}

void RestrictedStore::ensurePath(const StorePath & path)
{
    if (!goal.isAllowed(path))
        throw InvalidPath("cannot substitute unknown path '%s' in recursive Nix", printStorePath(path));
    /* Nothing to be done; 'path' must already be valid. */
}

void RestrictedStore::registerDrvOutput(const Realisation & info)
// XXX: This should probably be allowed as a no-op if the realisation
// corresponds to an allowed derivation
{
    throw Error("registerDrvOutput");
}

void RestrictedStore::queryRealisationUncached(
    const DrvOutput & id, Callback<std::shared_ptr<const Realisation>> callback) noexcept
// XXX: This should probably be allowed if the realisation corresponds to
// an allowed derivation
{
    if (!goal.isAllowed(id))
        callback(nullptr);
    next->queryRealisation(id, std::move(callback));
}

void RestrictedStore::buildPaths(
    const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    for (auto & result : buildPathsWithResults(paths, buildMode, evalStore))
        if (!result.success())
            result.rethrow();
}

std::vector<KeyedBuildResult> RestrictedStore::buildPathsWithResults(
    const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    assert(!evalStore);

    if (buildMode != bmNormal)
        throw Error("unsupported build mode");

    StorePathSet newPaths;
    std::set<Realisation> newRealisations;

    for (auto & req : paths) {
        if (!goal.isAllowed(req))
            throw InvalidPath("cannot build '%s' in recursive Nix because path is unknown", req.to_string(*next));
    }

    auto results = next->buildPathsWithResults(paths, buildMode);

    for (auto & result : results) {
        for (auto & [outputName, output] : result.builtOutputs) {
            newPaths.insert(output.outPath);
            newRealisations.insert(output);
        }
    }

    StorePathSet closure;
    next->computeFSClosure(newPaths, closure);
    for (auto & path : closure)
        goal.addDependency(path);
    for (auto & real : Realisation::closure(*next, newRealisations))
        goal.addedDrvOutputs.insert(real.id);

    return results;
}

static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

static StorePath pathPartOfReq(const DerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const DerivedPath::Opaque & bo) { return bo.path; },
            [&](const DerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

bool RestrictionContext::isAllowed(const DerivedPath & req)
{
    return isAllowed(pathPartOfReq(req));
}

void RestrictedStore::queryMissing(
    const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild,
    StorePathSet & willSubstitute,
    StorePathSet & unknown,
    uint64_t & downloadSize,
    uint64_t & narSize)
{
    /* This is slightly impure since it leaks information to the
       client about what paths will be built/substituted or are
       already present. Probably not a big deal. */

    std::vector<DerivedPath> allowed;
    for (auto & req : targets) {
        if (goal.isAllowed(req))
            allowed.emplace_back(req);
        else
            unknown.insert(pathPartOfReq(req));
    }

    next->queryMissing(allowed, willBuild, willSubstitute, unknown, downloadSize, narSize);
}

}
