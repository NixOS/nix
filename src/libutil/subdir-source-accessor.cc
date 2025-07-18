#include "nix/util/source-accessor.hh"

namespace nix {

struct SubdirSourceAccessor : SourceAccessor
{
    ref<SourceAccessor> parent;

    CanonPath subdirectory;

    SubdirSourceAccessor(ref<SourceAccessor> && parent, CanonPath && subdirectory)
        : parent(std::move(parent))
        , subdirectory(std::move(subdirectory))
    {
        displayPrefix.clear();
    }

    std::string readFile(const CanonPath & path) override
    {
        return parent->readFile(subdirectory / path);
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        return parent->readFile(subdirectory / path, sink, sizeCallback);
    }

    bool pathExists(const CanonPath & path) override
    {
        return parent->pathExists(subdirectory / path);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        return parent->maybeLstat(subdirectory / path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return parent->readDirectory(subdirectory / path);
    }

    std::string readLink(const CanonPath & path) override
    {
        return parent->readLink(subdirectory / path);
    }

    std::string showPath(const CanonPath & path) override
    {
        return displayPrefix + parent->showPath(subdirectory / path) + displaySuffix;
    }
};

ref<SourceAccessor> projectSubdirSourceAccessor(ref<SourceAccessor> parent, CanonPath subdirectory)
{
    return make_ref<SubdirSourceAccessor>(std::move(parent), std::move(subdirectory));
}

} // namespace nix
