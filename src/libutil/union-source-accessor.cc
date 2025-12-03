#include "nix/util/source-accessor.hh"

namespace nix {

struct UnionSourceAccessor : SourceAccessor
{
    std::vector<ref<SourceAccessor>> accessors;

    UnionSourceAccessor(std::vector<ref<SourceAccessor>> _accessors)
        : accessors(std::move(_accessors))
    {
        displayPrefix.clear();
    }

    std::string readFile(const CanonPath & path) override
    {
        for (auto & accessor : accessors) {
            auto st = accessor->maybeLstat(path);
            if (st)
                return accessor->readFile(path);
        }
        throw FileNotFound("path '%s' does not exist", showPath(path));
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        for (auto & accessor : accessors) {
            auto st = accessor->maybeLstat(path);
            if (st)
                return st;
        }
        return std::nullopt;
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        DirEntries result;
        for (auto & accessor : accessors) {
            auto st = accessor->maybeLstat(path);
            if (!st)
                continue;
            for (auto & entry : accessor->readDirectory(path))
                // Don't override entries from previous accessors.
                result.insert(entry);
        }
        return result;
    }

    std::string readLink(const CanonPath & path) override
    {
        for (auto & accessor : accessors) {
            auto st = accessor->maybeLstat(path);
            if (st)
                return accessor->readLink(path);
        }
        throw FileNotFound("path '%s' does not exist", showPath(path));
    }

    std::string showPath(const CanonPath & path) override
    {
        for (auto & accessor : accessors)
            return accessor->showPath(path);
        return SourceAccessor::showPath(path);
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        for (auto & accessor : accessors) {
            auto p = accessor->getPhysicalPath(path);
            if (p)
                return p;
        }
        return std::nullopt;
    }

    std::pair<CanonPath, std::optional<std::string>> getFingerprint(const CanonPath & path) override
    {
        if (fingerprint)
            return {path, fingerprint};
        for (auto & accessor : accessors) {
            auto [subpath, fingerprint] = accessor->getFingerprint(path);
            if (fingerprint)
                return {subpath, fingerprint};
        }
        return {path, std::nullopt};
    }
};

ref<SourceAccessor> makeUnionSourceAccessor(std::vector<ref<SourceAccessor>> && accessors)
{
    return make_ref<UnionSourceAccessor>(std::move(accessors));
}

} // namespace nix
