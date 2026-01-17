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

    NarAccessorImpl(Source & source)
        : root{parseNarListing(source)}
    {
    }

    NarAccessorImpl(Source & source, GetNarBytes getNarBytes)
        : root{parseNarListing(source)}
        , getNarBytes{std::move(getNarBytes)}
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

ref<NarAccessor> makeNarAccessor(Source & source)
{
    return make_ref<NarAccessorImpl>(source);
}

ref<NarAccessor> makeLazyNarAccessor(NarListing listing, GetNarBytes getNarBytes)
{
    return make_ref<NarAccessorImpl>(std::move(listing), getNarBytes);
}

ref<NarAccessor> makeLazyNarAccessor(Source & source, GetNarBytes getNarBytes)
{
    return make_ref<NarAccessorImpl>(source, getNarBytes);
}

GetNarBytes seekableGetNarBytes(const std::filesystem::path & path)
{
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        throw NativeSysError("opening NAR cache file %s", path);

    return [inner = seekableGetNarBytes(fd.get()), fd = make_ref<AutoCloseFD>(std::move(fd))](
               uint64_t offset, uint64_t length, Sink & sink) { return inner(offset, length, sink); };
}

GetNarBytes seekableGetNarBytes(Descriptor fd)
{
    return [fd](uint64_t offset_, uint64_t left, Sink & sink) {
        std::array<char, 64 * 1024> buf;

        off_t offset = offset_;

        while (left) {
            checkInterrupt();
            auto limit = std::min<decltype(buf)::size_type>(left, buf.size());
#ifdef _WIN32
            OVERLAPPED ov = {};
            ov.Offset = static_cast<DWORD>(offset);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD n;
            if (!ReadFile(fd, buf.data(), static_cast<DWORD>(limit), &n, &ov))
                throw nix::windows::WinError("reading %1% NAR bytes at offset %2%", left, offset);
#else
            ssize_t n = pread(fd, buf.data(), limit, offset);
            if (n == -1) {
                if (errno == EINTR)
                    continue;
                throw SysError("reading %1% NAR bytes at offset %2%", left, offset);
            }
#endif
            if (n == 0)
                throw EndOfFile("unexpected end-of-file");
            assert(static_cast<uint64_t>(n) <= left);
            sink(std::string_view(buf.data(), n));
            offset += n;
            left -= n;
        }
    };
}

} // namespace nix
