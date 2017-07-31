#include "archive.hh"
#include "fs-accessor.hh"
#include "store-api.hh"
#include "globals.hh"
#include "compression.hh"
#include "derivations.hh"

namespace nix {

LocalFSStore::LocalFSStore(const Params & params)
    : Store(params)
{
}

struct LocalStoreAccessor : public FSAccessor
{
    ref<LocalFSStore> store;

    LocalStoreAccessor(ref<LocalFSStore> store) : store(store) { }

    Path toRealPath(const Path & path)
    {
        Path storePath = store->toStorePath(path);
        if (!store->isValidPath(storePath))
            throw InvalidPath(format("path '%1%' is not a valid store path") % storePath);
        return store->getRealStoreDir() + std::string(path, store->storeDir.size());
    }

    FSAccessor::Stat stat(const Path & path) override
    {
        auto realPath = toRealPath(path);

        struct stat st;
        if (lstat(realPath.c_str(), &st)) {
            if (errno == ENOENT || errno == ENOTDIR) return {Type::tMissing, 0, false};
            throw SysError(format("getting status of '%1%'") % path);
        }

        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            throw Error(format("file '%1%' has unsupported type") % path);

        return {
            S_ISREG(st.st_mode) ? Type::tRegular :
            S_ISLNK(st.st_mode) ? Type::tSymlink :
            Type::tDirectory,
            S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
            S_ISREG(st.st_mode) && st.st_mode & S_IXUSR};
    }

    StringSet readDirectory(const Path & path) override
    {
        auto realPath = toRealPath(path);

        auto entries = nix::readDirectory(realPath);

        StringSet res;
        for (auto & entry : entries)
            res.insert(entry.name);

        return res;
    }

    std::string readFile(const Path & path) override
    {
        return nix::readFile(toRealPath(path));
    }

    std::string readLink(const Path & path) override
    {
        return nix::readLink(toRealPath(path));
    }
};

ref<FSAccessor> LocalFSStore::getFSAccessor()
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>(
            std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())));
}

void LocalFSStore::narFromPath(const Path & path, Sink & sink)
{
    if (!isValidPath(path))
        throw Error(format("path '%s' is not valid") % path);
    dumpPath(getRealStoreDir() + std::string(path, storeDir.size()), sink);
}

const string LocalFSStore::drvsLogDir = "drvs";



std::shared_ptr<std::string> LocalFSStore::getBuildLog(const Path & path_)
{
    auto path(path_);

    assertStorePath(path);


    if (!isDerivation(path)) {
        try {
            path = queryPathInfo(path)->deriver;
        } catch (InvalidPath &) {
            return nullptr;
        }
        if (path == "") return nullptr;
    }

    string baseName = baseNameOf(path);

    for (int j = 0; j < 2; j++) {

        Path logPath =
            j == 0
            ? fmt("%s/%s/%s/%s", logDir, drvsLogDir, string(baseName, 0, 2), string(baseName, 2))
            : fmt("%s/%s/%s", logDir, drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            return std::make_shared<std::string>(readFile(logPath));

        else if (pathExists(logBz2Path)) {
            try {
                return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) { }
        }

    }

    return nullptr;
}

}
