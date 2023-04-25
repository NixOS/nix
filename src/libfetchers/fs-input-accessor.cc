#include "fs-input-accessor.hh"
#include "store-api.hh"

namespace nix {

struct FSInputAccessorImpl : FSInputAccessor
{
    CanonPath root;
    std::optional<std::set<CanonPath>> allowedPaths;
    MakeNotAllowedError makeNotAllowedError;

    FSInputAccessorImpl(
        const CanonPath & root,
        std::optional<std::set<CanonPath>> && allowedPaths,
        MakeNotAllowedError && makeNotAllowedError)
        : root(root)
        , allowedPaths(std::move(allowedPaths))
        , makeNotAllowedError(std::move(makeNotAllowedError))
    {
        displayPrefix = root.isRoot() ? "" : root.abs();
    }

    std::string readFile(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return nix::readFile(absPath.abs());
    }

    bool pathExists(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        return isAllowed(absPath) && nix::pathExists(absPath.abs());
    }

    Stat lstat(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        auto st = nix::lstat(absPath.abs());
        return Stat {
            .type =
                S_ISREG(st.st_mode) ? tRegular :
                S_ISDIR(st.st_mode) ? tDirectory :
                S_ISLNK(st.st_mode) ? tSymlink :
                tMisc,
            .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
        };
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        DirEntries res;
        for (auto & entry : nix::readDirectory(absPath.abs())) {
            std::optional<Type> type;
            switch (entry.type) {
            case DT_REG: type = Type::tRegular; break;
            case DT_LNK: type = Type::tSymlink; break;
            case DT_DIR: type = Type::tDirectory; break;
            }
            if (isAllowed(absPath + entry.name))
                res.emplace(entry.name, type);
        }
        return res;
    }

    std::string readLink(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return nix::readLink(absPath.abs());
    }

    CanonPath makeAbsPath(const CanonPath & path)
    {
        return root + path;
    }

    void checkAllowed(const CanonPath & absPath) override
    {
        if (!isAllowed(absPath))
            throw makeNotAllowedError
                ? makeNotAllowedError(absPath)
                : RestrictedPathError("access to path '%s' is forbidden", absPath);
    }

    bool isAllowed(const CanonPath & absPath)
    {
        if (!absPath.isWithin(root))
            return false;

        if (allowedPaths) {
            auto p = absPath.removePrefix(root);
            if (!p.isAllowed(*allowedPaths))
                return false;
        }

        return true;
    }

    void allowPath(CanonPath path) override
    {
        if (allowedPaths)
            allowedPaths->insert(std::move(path));
    }

    bool hasAccessControl() override
    {
        return (bool) allowedPaths;
    }

    std::optional<CanonPath> getPhysicalPath(const CanonPath & path) override
    {
        return makeAbsPath(path);
    }
};

ref<FSInputAccessor> makeFSInputAccessor(
    const CanonPath & root,
    std::optional<std::set<CanonPath>> && allowedPaths,
    MakeNotAllowedError && makeNotAllowedError)
{
    return make_ref<FSInputAccessorImpl>(root, std::move(allowedPaths), std::move(makeNotAllowedError));
}

ref<FSInputAccessor> makeStorePathAccessor(
    ref<Store> store,
    const StorePath & storePath,
    MakeNotAllowedError && makeNotAllowedError)
{
    return makeFSInputAccessor(CanonPath(store->toRealPath(storePath)), {}, std::move(makeNotAllowedError));
}

SourcePath getUnfilteredRootPath(CanonPath path)
{
    static auto rootFS = makeFSInputAccessor(CanonPath::root);
    return {rootFS, path};
}

}
