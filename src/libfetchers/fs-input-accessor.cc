#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"

namespace nix {

struct FSInputAccessorImpl : FSInputAccessor, PosixSourceAccessor
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
    }

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        PosixSourceAccessor::readFile(absPath, sink, sizeCallback);
    }

    bool pathExists(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        return isAllowed(absPath) && PosixSourceAccessor::pathExists(absPath);
    }

    Stat lstat(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return PosixSourceAccessor::lstat(absPath);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        DirEntries res;
        for (auto & entry : PosixSourceAccessor::readDirectory(absPath))
            if (isAllowed(absPath + entry.first))
                res.emplace(entry);
        return res;
    }

    std::string readLink(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return PosixSourceAccessor::readLink(absPath);
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
