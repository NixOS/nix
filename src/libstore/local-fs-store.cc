#include "archive.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "globals.hh"
#include "compression.hh"
#include "derivations.hh"

namespace nix {

LocalFSStore::LocalFSStore(const Params & params)
    : Store(params)
{
}

struct LocalStoreAccessor : PosixSourceAccessor
{
    ref<LocalFSStore> store;
    bool requireValidPath;

    LocalStoreAccessor(ref<LocalFSStore> store, bool requireValidPath)
        : store(store)
        , requireValidPath(requireValidPath)
    { }

    CanonPath toRealPath(const CanonPath & path)
    {
        auto [storePath, rest] = store->toStorePath(path.abs());
        if (requireValidPath && !store->isValidPath(storePath))
            throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
        return CanonPath(store->getRealStoreDir()) / storePath.to_string() / CanonPath(rest);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        /* Handle the case where `path` is (a parent of) the store. */
        if (isDirOrInDir(store->storeDir, path.abs()))
            return Stat{ .type = tDirectory };

        return PosixSourceAccessor::maybeLstat(toRealPath(path));
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return PosixSourceAccessor::readDirectory(toRealPath(path));
    }

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override
    {
        return PosixSourceAccessor::readFile(toRealPath(path), sink, sizeCallback);
    }

    std::string readLink(const CanonPath & path) override
    {
        return PosixSourceAccessor::readLink(toRealPath(path));
    }
};

ref<SourceAccessor> LocalFSStore::getFSAccessor(bool requireValidPath)
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>(
            std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())),
        requireValidPath);
}

void LocalFSStore::narFromPath(const StorePath & path, Sink & sink)
{
    if (!isValidPath(path))
        throw Error("path '%s' is not valid", printStorePath(path));
    dumpPath(getRealStoreDir() + std::string(printStorePath(path), storeDir.size()), sink);
}

const std::string LocalFSStore::drvsLogDir = "drvs";

std::optional<std::string> LocalFSStore::getBuildLogExact(const StorePath & path)
{
    auto baseName = path.to_string();

    for (int j = 0; j < 2; j++) {

        Path logPath =
            j == 0
            ? fmt("%s/%s/%s/%s", logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
            : fmt("%s/%s/%s", logDir, drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            return readFile(logPath);

        else if (pathExists(logBz2Path)) {
            try {
                return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) { }
        }

    }

    return std::nullopt;
}

}
