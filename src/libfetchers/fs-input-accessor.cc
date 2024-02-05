#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"

namespace nix {

struct FSInputAccessor : InputAccessor, PosixSourceAccessor
{
    CanonPath root;

    FSInputAccessor(const CanonPath & root)
        : root(root)
    {
        displayPrefix = root.isRoot() ? "" : root.abs();
    }

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override
    {
        auto absPath = makeAbsPath(path);
        PosixSourceAccessor::readFile(absPath, sink, sizeCallback);
    }

    bool pathExists(const CanonPath & path) override
    {
        return PosixSourceAccessor::pathExists(makeAbsPath(path));
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        return PosixSourceAccessor::maybeLstat(makeAbsPath(path));
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        DirEntries res;
        for (auto & entry : PosixSourceAccessor::readDirectory(makeAbsPath(path)))
            res.emplace(entry);
        return res;
    }

    std::string readLink(const CanonPath & path) override
    {
        return PosixSourceAccessor::readLink(makeAbsPath(path));
    }

    CanonPath makeAbsPath(const CanonPath & path)
    {
        return root / path;
    }

    std::optional<CanonPath> getPhysicalPath(const CanonPath & path) override
    {
        return makeAbsPath(path);
    }
};

ref<InputAccessor> makeFSInputAccessor(const CanonPath & root)
{
    return make_ref<FSInputAccessor>(root);
}

ref<InputAccessor> makeStorePathAccessor(
    ref<Store> store,
    const StorePath & storePath)
{
    return makeFSInputAccessor(CanonPath(store->toRealPath(storePath)));
}

SourcePath getUnfilteredRootPath(CanonPath path)
{
    static auto rootFS = makeFSInputAccessor(CanonPath::root);
    return {rootFS, path};
}

}
