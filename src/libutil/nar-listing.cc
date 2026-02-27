#include "nix/util/nar-listing.hh"
#include "nix/util/archive.hh"
#include "nix/util/error.hh"

#include <stack>

namespace nix {

NarListing parseNarListing(Source & source)
{
    struct NarMemberConstructor : CreateRegularFileSink
    {
    private:

        NarListing::Regular & regular;

        uint64_t & pos;

    public:

        NarMemberConstructor(NarListing::Regular & reg, uint64_t & pos)
            : regular(reg)
            , pos(pos)
        {
        }

        void isExecutable() override
        {
            regular.executable = true;
        }

        void preallocateContents(uint64_t size) override
        {
            regular.contents.fileSize = size;
            regular.contents.narOffset = pos;
        }

        void operator()(std::string_view data) override {}
    };

    struct NarIndexer : FileSystemObjectSink, Source
    {
        std::optional<NarListing> root;
        Source & source;

        std::stack<NarListing *> parents;

        uint64_t pos = 0;

        NarIndexer(Source & source)
            : source(source)
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
                root = std::move(member);
                parents.push(&*root);
                return *root;
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

        void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)> func) override
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
            /* We know the downcast will succeed because we just added this */
            auto & reg = std::get<NarListing::Regular>(nm.raw);
            NarMemberConstructor nmc{reg, pos};
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

        void skip(size_t len) override
        {
            source.skip(len);
            pos += len;
        }
    };

    NarIndexer indexer(source);
    parseDump(indexer, indexer);
    return std::move(*indexer.root);
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
    default:
        throw Error("file '%s' has an unsupported type", accessor.showPath(path));
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
