#include "nix/util/nar-accessor.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/archive.hh"
#include "nix/util/error.hh"

#include <map>
#include <stack>

namespace nix {

struct NarMemberConstructor : CreateRegularFileSink
{
private:

    NarListing & narMember;

    uint64_t & pos;

public:

    NarMemberConstructor(NarListing & nm, uint64_t & pos)
        : narMember(nm)
        , pos(pos)
    {
    }

    void isExecutable() override
    {
        auto * reg = std::get_if<NarListing::Regular>(&narMember.raw);
        if (reg)
            reg->executable = true;
    }

    void preallocateContents(uint64_t size) override
    {
        auto * reg = std::get_if<NarListing::Regular>(&narMember.raw);
        if (reg) {
            reg->contents.fileSize = size;
            reg->contents.narOffset = pos;
        }
    }

    void operator()(std::string_view data) override {}
};

struct NarAccessor : public SourceAccessor
{
    std::optional<const std::string> nar;

    GetNarBytes getNarBytes;

    NarListing root;

    struct NarIndexer : FileSystemObjectSink, Source
    {
        NarAccessor & acc;
        Source & source;

        std::stack<NarListing *> parents;

        bool isExec = false;

        uint64_t pos = 0;

        NarIndexer(NarAccessor & acc, Source & source)
            : acc(acc)
            , source(source)
        {
        }

        NarListing & createMember(const CanonPath & path, NarListing member)
        {
            size_t level = 0;
            for (auto _ : path) {
                (void) _;
                ++level;
            }

            while (parents.size() > level)
                parents.pop();

            if (parents.empty()) {
                acc.root = std::move(member);
                parents.push(&acc.root);
                return acc.root;
            } else {
                auto * parentDir = std::get_if<NarListing::Directory>(&parents.top()->raw);
                if (!parentDir)
                    throw Error("NAR file missing parent directory of path '%s'", path);
                auto result = parentDir->entries.emplace(*path.baseName(), std::move(member));
                parents.push(&result.first->second);
                return result.first->second;
            }
        }

        void createDirectory(const CanonPath & path) override
        {
            createMember(path, NarListing::Directory{});
        }

        void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func) override
        {
            auto & nm = createMember(
                path,
                NarListing::Regular{
                    .executable = false,
                    .contents =
                        NarListingRegularFile{
                            .fileSize = 0,
                            .narOffset = pos,
                        },
                });
            NarMemberConstructor nmc{nm, pos};
            nmc.skipContents = true; /* Don't care about contents. */
            func(nmc);
        }

        void createSymlink(const CanonPath & path, const std::string & target) override
        {
            createMember(path, NarListing::Symlink{.target = target});
        }

        size_t read(char * data, size_t len) override
        {
            auto n = source.read(data, len);
            pos += n;
            return n;
        }
    };

    NarAccessor(std::string && _nar)
        : nar(_nar)
    {
        StringSource source(*nar);
        NarIndexer indexer(*this, source);
        parseDump(indexer, indexer);
    }

    NarAccessor(Source & source)
    {
        NarIndexer indexer(*this, source);
        parseDump(indexer, indexer);
    }

    NarAccessor(Source & source, GetNarBytes getNarBytes)
        : getNarBytes(std::move(getNarBytes))
    {
        NarIndexer indexer(*this, source);
        parseDump(indexer, indexer);
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

template<bool deep>
using ListNarResult = std::conditional_t<deep, NarListing, ShallowNarListing>;

template<bool deep>
static ListNarResult<deep> listNarImpl(SourceAccessor & accessor, const CanonPath & path)
{
    auto st = accessor.lstat(path);

    switch (st.type) {
    case SourceAccessor::Type::tRegular:
        return typename ListNarResult<deep>::Regular{
            .executable = st.isExecutable,
            .contents =
                NarListingRegularFile{
                    .fileSize = st.fileSize,
                    .narOffset = st.narOffset && *st.narOffset ? st.narOffset : std::nullopt,
                },
        };
    case SourceAccessor::Type::tDirectory: {
        typename ListNarResult<deep>::Directory dir;
        for (const auto & [name, type] : accessor.readDirectory(path)) {
            if constexpr (deep) {
                dir.entries.emplace(name, listNarImpl<true>(accessor, path / name));
            } else {
                dir.entries.emplace(name, fso::Opaque{});
            }
        }
        return dir;
    }
    case SourceAccessor::Type::tSymlink:
        return typename ListNarResult<deep>::Symlink{
            .target = accessor.readLink(path),
        };
    case SourceAccessor::Type::tBlock:
    case SourceAccessor::Type::tChar:
    case SourceAccessor::Type::tSocket:
    case SourceAccessor::Type::tFifo:
    case SourceAccessor::Type::tUnknown:
        assert(false); // cannot happen for NARs
    }
}

NarListing listNarDeep(SourceAccessor & accessor, const CanonPath & path)
{
    return listNarImpl<true>(accessor, path);
}

ShallowNarListing listNarShallow(SourceAccessor & accessor, const CanonPath & path)
{
    return listNarImpl<false>(accessor, path);
}

} // namespace nix
