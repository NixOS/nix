#include "archive.hh"
#include "source-accessor.hh"
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

struct LocalStoreAccessor : public SourceAccessor
{
    ref<LocalFSStore> store;
    bool requireValidPath;

    LocalStoreAccessor(ref<LocalFSStore> store, bool requireValidPath)
        : store(store)
        , requireValidPath(requireValidPath)
    { }

    Path toRealPath(const CanonPath & path)
    {
        auto storePath = store->toStorePath(path.abs()).first;
        if (requireValidPath && !store->isValidPath(storePath))
            throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
        return store->getRealStoreDir() + path.abs().substr(store->storeDir.size());
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        auto realPath = toRealPath(path);

        // FIXME: use PosixSourceAccessor.
        struct stat st;
        if (::lstat(realPath.c_str(), &st)) {
            if (errno == ENOENT || errno == ENOTDIR) return std::nullopt;
            throw SysError("getting status of '%1%'", path);
        }

        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            throw Error("file '%1%' has unsupported type", path);

        return {{
            S_ISREG(st.st_mode) ? Type::tRegular :
            S_ISLNK(st.st_mode) ? Type::tSymlink :
            Type::tDirectory,
            S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
            S_ISREG(st.st_mode) && st.st_mode & S_IXUSR}};
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto realPath = toRealPath(path);

        auto entries = nix::readDirectory(realPath);

        DirEntries res;
        for (auto & entry : entries)
            res.insert_or_assign(entry.name, std::nullopt);

        return res;
    }

    std::string readFile(const CanonPath & path) override
    {
        return nix::readFile(toRealPath(path));
    }

    std::string readLink(const CanonPath & path) override
    {
        return nix::readLink(toRealPath(path));
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
