#include "nix/util/mounted-source-accessor.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

struct MountedSourceAccessorImpl : MountedSourceAccessor
{
    boost::concurrent_flat_map<CanonPath, ref<SourceAccessor>> mounts;

    MountedSourceAccessorImpl(std::map<CanonPath, ref<SourceAccessor>> _mounts)
    {
        displayPrefix.clear();

        // Currently we require a root filesystem. This could be relaxed.
        assert(_mounts.contains(CanonPath::root));

        for (auto & [path, accessor] : _mounts)
            mount(path, accessor);

        // FIXME: return dummy parent directories automatically?
    }

    std::string readFile(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->readFile(subpath);
    }

    Stat lstat(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->lstat(subpath);
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

    std::pair<ref<SourceAccessor>, CanonPath> resolve(CanonPath path)
    {
        // Find the nearest parent of `path` that is a mount point.
        std::vector<std::string> subpath;
        while (true) {
            if (auto mount = getMount(path)) {
                std::reverse(subpath.begin(), subpath.end());
                return {ref(mount), CanonPath(subpath)};
            }

            assert(!path.isRoot());
            subpath.push_back(std::string(*path.baseName()));
            path.pop();
        }
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        auto [accessor, subpath] = resolve(path);
        return accessor->getPhysicalPath(subpath);
    }

    void mount(CanonPath mountPoint, ref<SourceAccessor> accessor) override
    {
        mounts.emplace(std::move(mountPoint), std::move(accessor));
    }

    std::shared_ptr<SourceAccessor> getMount(CanonPath mountPoint) override
    {
        if (auto res = getConcurrent(mounts, mountPoint))
            return *res;
        else
            return nullptr;
    }

    std::pair<CanonPath, std::optional<std::string>> getFingerprint(const CanonPath & path) override
    {
        if (fingerprint)
            return {path, fingerprint};
        auto [accessor, subpath] = resolve(path);
        return accessor->getFingerprint(subpath);
    }
};

ref<MountedSourceAccessor> makeMountedSourceAccessor(std::map<CanonPath, ref<SourceAccessor>> mounts)
{
    return make_ref<MountedSourceAccessorImpl>(std::move(mounts));
}

} // namespace nix
