#include "mounted-input-accessor.hh"

namespace nix {

struct MountedInputAccessor : InputAccessor
{
    std::map<CanonPath, ref<InputAccessor>> mounts;

    MountedInputAccessor(std::map<CanonPath, ref<InputAccessor>> _mounts)
        : mounts(std::move(_mounts))
    {
        displayPrefix.clear();

        // Currently we require a root filesystem. This could be relaxed.
        assert(mounts.contains(CanonPath::root));

        // FIXME: return dummy parent directories automatically?
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
        return displayPrefix + accessor->showPath(subpath) + displaySuffix;
    }

    std::pair<ref<InputAccessor>, CanonPath> resolve(CanonPath path)
    {
        // Find the nearest parent of `path` that is a mount point.
        std::vector<std::string> subpath;
        while (true) {
            auto i = mounts.find(path);
            if (i != mounts.end()) {
                std::reverse(subpath.begin(), subpath.end());
                return {i->second, CanonPath(subpath)};
            }

            assert(!path.isRoot());
            subpath.push_back(std::string(*path.baseName()));
            path.pop();
        }
    }
};

ref<InputAccessor> makeMountedInputAccessor(std::map<CanonPath, ref<InputAccessor>> mounts)
{
    return make_ref<MountedInputAccessor>(std::move(mounts));
}

}
