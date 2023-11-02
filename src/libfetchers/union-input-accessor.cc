#include "union-input-accessor.hh"

namespace nix {

struct UnionInputAccessor : InputAccessor
{
    std::map<CanonPath, ref<InputAccessor>> mounts;

    UnionInputAccessor(std::map<CanonPath, ref<InputAccessor>> _mounts)
        : mounts(std::move(_mounts))
    {
        // Currently we require a root filesystem. This could be relaxed.
        assert(mounts.contains(CanonPath::root));

        // FIXME: should check that every mount point exists. Or we
        // could return dummy parent directories automatically.
    }

    std::string readFile(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->readFile(subpath);
    }

    bool pathExists(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->pathExists(subpath);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->maybeLstat(subpath);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->readDirectory(subpath);
    }

    std::string readLink(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->readLink(subpath);
    }

    std::string showPath(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->showPath(subpath);
    }

    std::pair<ref<InputAccessor>, CanonPath> resolve(CanonPath path)
    {
        // Find the nearest parent of `path` that is a mount point.
        std::vector<std::string> ss;
        while (true) {
            auto i = mounts.find(path);
            if (i != mounts.end()) {
                auto subpath = CanonPath::root;
                for (auto j = ss.rbegin(); j != ss.rend(); ++j)
                    subpath.push(*j);
                return {i->second, std::move(subpath)};
            }

            assert(!path.isRoot());
            ss.push_back(std::string(*path.baseName()));
            path.pop();
        }
    }
};

ref<InputAccessor> makeUnionInputAccessor(std::map<CanonPath, ref<InputAccessor>> mounts)
{
    return make_ref<UnionInputAccessor>(std::move(mounts));
}

}
