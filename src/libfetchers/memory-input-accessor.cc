#include "memory-input-accessor.hh"

namespace nix {

struct MemoryInputAccessorImpl : MemoryInputAccessor
{
    std::map<CanonPath, std::string> files;

    std::string readFile(const CanonPath & path) override
    {
        auto i = files.find(path);
        if (i == files.end())
            throw Error("file '%s' does not exist", path);
        return i->second;
    }

    bool pathExists(const CanonPath & path) override
    {
        auto i = files.find(path);
        return i != files.end();
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        auto i = files.find(path);
        if (i != files.end())
            return Stat { .type = tRegular, .isExecutable = false };
        return std::nullopt;
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return {};
    }

    std::string readLink(const CanonPath & path) override
    {
        throw UnimplementedError("MemoryInputAccessor::readLink");
    }

    SourcePath addFile(CanonPath path, std::string && contents) override
    {
        files.emplace(path, std::move(contents));

        return {ref(shared_from_this()), std::move(path)};
    }
};

ref<MemoryInputAccessor> makeMemoryInputAccessor()
{
    return make_ref<MemoryInputAccessorImpl>();
}

}
