#include "nix/util/nar-accessor.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/error.hh"

namespace nix {

struct NarAccessor : public SourceAccessor
{
    std::optional<const std::string> nar;

    GetNarBytes getNarBytes;

    NarListing root;

    NarAccessor(std::string && _nar)
        : nar(_nar)
    {
        StringSource source(*nar);
        root = parseNarListing(source);
    }

    NarAccessor(Source & source)
    {
        root = parseNarListing(source);
    }

    NarAccessor(Source & source, GetNarBytes getNarBytes)
        : getNarBytes(std::move(getNarBytes))
    {
        root = parseNarListing(source);
    }

    NarAccessor(NarListing && listing, GetNarBytes getNarBytes)
        : getNarBytes(getNarBytes)
        , root{listing}
    {
    }

    NarListing * find(const CanonPath & path)
    {
        NarListing * current = &root;

        for (const auto & i : path) {
            auto * dir = std::get_if<NarListing::Directory>(&current->raw);
            if (!dir)
                return nullptr;
            auto * child = nix::get(dir->entries, i);
            if (!child)
                return nullptr;
            current = child;
        }

        return current;
    }

    NarListing & get(const CanonPath & path)
    {
        auto result = find(path);
        if (!result)
            throw Error("NAR file does not contain path '%1%'", path);
        return *result;
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        auto i = find(path);
        if (!i)
            return std::nullopt;
        return std::visit(
            overloaded{
                [](const NarListing::Regular & r) -> Stat {
                    return {
                        .type = Type::tRegular,
                        .fileSize = r.contents.fileSize,
                        .isExecutable = r.executable,
                        .narOffset = r.contents.narOffset,
                    };
                },
                [](const NarListing::Directory &) -> Stat {
                    return {
                        .type = Type::tDirectory,
                    };
                },
                [](const NarListing::Symlink &) -> Stat {
                    return {
                        .type = Type::tSymlink,
                    };
                },
            },
            i->raw);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto & i = get(path);

        auto * dir = std::get_if<NarListing::Directory>(&i.raw);
        if (!dir)
            throw Error("path '%1%' inside NAR file is not a directory", path);

        DirEntries res;
        for (const auto & [name, child] : dir->entries)
            res.insert_or_assign(name, std::nullopt);

        return res;
    }

    std::string readFile(const CanonPath & path) override
    {
        auto & i = get(path);
        auto * reg = std::get_if<NarListing::Regular>(&i.raw);
        if (!reg)
            throw Error("path '%1%' inside NAR file is not a regular file", path);

        if (getNarBytes)
            return getNarBytes(*reg->contents.narOffset, *reg->contents.fileSize);

        assert(nar);
        return std::string(*nar, *reg->contents.narOffset, *reg->contents.fileSize);
    }

    std::string readLink(const CanonPath & path) override
    {
        auto & i = get(path);
        auto * sym = std::get_if<NarListing::Symlink>(&i.raw);
        if (!sym)
            throw Error("path '%1%' inside NAR file is not a symlink", path);
        return sym->target;
    }
};

ref<SourceAccessor> makeNarAccessor(std::string && nar)
{
    return make_ref<NarAccessor>(std::move(nar));
}

ref<SourceAccessor> makeNarAccessor(Source & source)
{
    return make_ref<NarAccessor>(source);
}

ref<SourceAccessor> makeLazyNarAccessor(NarListing listing, GetNarBytes getNarBytes)
{
    return make_ref<NarAccessor>(std::move(listing), getNarBytes);
}

ref<SourceAccessor> makeLazyNarAccessor(Source & source, GetNarBytes getNarBytes)
{
    return make_ref<NarAccessor>(source, getNarBytes);
}

GetNarBytes seekableGetNarBytes(const std::filesystem::path & path)
{
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        throw NativeSysError("opening NAR cache file %s", path);

    return [inner = seekableGetNarBytes(fd.get()), fd = make_ref<AutoCloseFD>(std::move(fd))](
               uint64_t offset, uint64_t length) { return inner(offset, length); };
}

GetNarBytes seekableGetNarBytes(Descriptor fd)
{
    return [fd](uint64_t offset, uint64_t length) {
        if (lseek(fd, offset, SEEK_SET) == -1)
            throw SysError("seeking in file");

        std::string buf(length, 0);
        readFull(fd, buf.data(), length);

        return buf;
    };
}

} // namespace nix
