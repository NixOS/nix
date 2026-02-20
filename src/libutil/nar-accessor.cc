#include "nix/util/nar-accessor.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/error.hh"
#include "nix/util/signals.hh"

namespace nix {

struct NarAccessorImpl : NarAccessor
{
    NarListing root;

    GetNarBytes getNarBytes;

    const NarListing & getListing() const override
    {
        return root;
    }

    NarAccessorImpl(std::string && nar)
        : root{[&nar]() {
            StringSource source(nar);
            return parseNarListing(source);
        }()}
        , getNarBytes{[nar = std::move(nar)](uint64_t offset, uint64_t length, Sink & sink) {
            if (offset > nar.size() || length > nar.size() - offset)
                throw Error(
                    "reading invalid NAR bytes range: requested %1% bytes at offset %2%, but NAR has size %3%",
                    length,
                    offset,
                    nar.size());
            StringSource source(std::string_view(nar.data() + offset, length));
            source.drainInto(sink);
        }}
    {
    }

    NarAccessorImpl(NarListing && listing)
        : root{std::move(listing)}
    {
    }

    NarAccessorImpl(NarListing && listing, GetNarBytes getNarBytes)
        : root{std::move(listing)}
        , getNarBytes{std::move(getNarBytes)}
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

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        auto & i = get(path);
        auto * reg = std::get_if<NarListing::Regular>(&i.raw);
        if (!reg)
            throw Error("path '%1%' inside NAR file is not a regular file", path);

        assert(getNarBytes);
        sizeCallback(reg->contents.fileSize.value());
        return getNarBytes(reg->contents.narOffset.value(), reg->contents.fileSize.value(), sink);
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

ref<NarAccessor> makeNarAccessor(std::string && nar)
{
    return make_ref<NarAccessorImpl>(std::move(nar));
}

ref<NarAccessor> makeNarAccessor(NarListing listing)
{
    return make_ref<NarAccessorImpl>(std::move(listing));
}

ref<NarAccessor> makeLazyNarAccessor(NarListing listing, GetNarBytes getNarBytes)
{
    return make_ref<NarAccessorImpl>(std::move(listing), getNarBytes);
}

GetNarBytes seekableGetNarBytes(const std::filesystem::path & path)
{
    auto fd = openFileReadonly(path);
    if (!fd)
        throw NativeSysError("opening NAR cache file %s", PathFmt(path));

    return [inner = seekableGetNarBytes(fd.get()), fd = make_ref<AutoCloseFD>(std::move(fd))](
               uint64_t offset, uint64_t length, Sink & sink) { return inner(offset, length, sink); };
}

GetNarBytes seekableGetNarBytes(Descriptor fd)
{
    return [fd](uint64_t offset, uint64_t length, Sink & sink) {
        if (offset >= std::numeric_limits<off_t>::max()) /* Just in case off_t is not 64 bits. */
            throw Error("can't read %1% NAR bytes from offset %2%: offset too big", length, offset);
        if (length >= std::numeric_limits<size_t>::max()) /* Just in case size_t is 32 bits. */
            throw Error("can't read %1% NAR bytes from offset %2%: length is too big", length, offset);
        copyFdRange(fd, static_cast<off_t>(offset), static_cast<size_t>(length), sink);
    };
}

} // namespace nix
