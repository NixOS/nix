#include "archive.hh"
#include "fs-accessor.hh"
#include "store-api.hh"

namespace nix {

struct LocalStoreAccessor : public FSAccessor
{
    ref<Store> store;

    LocalStoreAccessor(ref<Store> store) : store(store) { }

    void assertStore(const Path & path)
    {
        Path storePath = toStorePath(path);
        if (!store->isValidPath(storePath))
            throw Error(format("path ‘%1%’ is not a valid store path") % storePath);
    }

    FSAccessor::Stat stat(const Path & path) override
    {
        assertStore(path);

        struct stat st;
        if (lstat(path.c_str(), &st)) {
            if (errno == ENOENT || errno == ENOTDIR) return {Type::tMissing, 0, false};
            throw SysError(format("getting status of ‘%1%’") % path);
        }

        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            throw Error(format("file ‘%1%’ has unsupported type") % path);

        return {
            S_ISREG(st.st_mode) ? Type::tRegular :
            S_ISLNK(st.st_mode) ? Type::tSymlink :
            Type::tDirectory,
            S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
            S_ISREG(st.st_mode) && st.st_mode & S_IXUSR};
    }

    StringSet readDirectory(const Path & path) override
    {
        assertStore(path);

        auto entries = nix::readDirectory(path);

        StringSet res;
        for (auto & entry : entries)
            res.insert(entry.name);

        return res;
    }

    std::string readFile(const Path & path) override
    {
        assertStore(path);
        return nix::readFile(path);
    }

    std::string readLink(const Path & path) override
    {
        assertStore(path);
        return nix::readLink(path);
    }
};

ref<FSAccessor> LocalFSStore::getFSAccessor()
{
    return make_ref<LocalStoreAccessor>(ref<Store>(shared_from_this()));
}

void LocalFSStore::narFromPath(const Path & path, Sink & sink)
{
    if (!isValidPath(path))
        throw Error(format("path ‘%s’ is not valid") % path);
    dumpPath(path, sink);
}

}
