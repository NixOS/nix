#include "nix/util/nar-listing.hh"
#include "nix/util/archive.hh"
#include "nix/util/error.hh"

namespace nix {

NarListing parseNarListing(Source & source)
{
    struct NarIndexer : FileSystemObjectSink, Source
    {
        std::optional<NarListing> root;
        Source & source;
        uint64_t pos = 0;

        /**
         * Pointer to where the next created member should be stored.
         * For root, this points to `root`. For children, it points into
         * the parent directory's entries map.
         */
        NarListing * currentDst = nullptr;

        NarIndexer(Source & source)
            : source(source)
        {
        }

        void createDirectory(DirectoryCreatedCallback callback) override
        {
            if (currentDst) {
                *currentDst = NarListing::Directory{};
            } else {
                root = NarListing::Directory{};
                currentDst = &*root;
            }

            struct Dir : OnDirectory
            {
                NarIndexer & indexer;
                NarListing::Directory & dir;

                Dir(NarIndexer & indexer, NarListing::Directory & dir)
                    : indexer(indexer)
                    , dir(dir)
                {
                }

                void createChild(std::string_view name, ChildCreatedCallback callback) override
                {
                    auto [it, inserted] = dir.entries.emplace(std::string{name}, NarListing{});
                    auto oldDst = indexer.currentDst;
                    indexer.currentDst = &it->second;

                    callback(indexer);

                    indexer.currentDst = oldDst;
                }
            } dir{*this, std::get<NarListing::Directory>(currentDst->raw)};

            callback(dir);
        }

        void createRegularFile(bool isExecutable, RegularFileCreatedCallback func) override
        {
            auto reg = NarListing::Regular{
                .executable = isExecutable,
                .contents =
                    NarListingRegularFile{
                        .fileSize = 0,
                        .narOffset = pos,
                    },
            };

            if (currentDst) {
                *currentDst = std::move(reg);
            } else {
                root = std::move(reg);
                currentDst = &*root;
            }

            auto & regRef = std::get<NarListing::Regular>(currentDst->raw);

            struct NarMemberConstructor : OnRegularFile
            {
                NarListing::Regular & regular;
                uint64_t & pos;

                NarMemberConstructor(NarListing::Regular & reg, uint64_t & pos)
                    : regular(reg)
                    , pos(pos)
                {
                    skipContents = true; /* Don't care about contents. */
                }

                void preallocateContents(uint64_t size) override
                {
                    regular.contents.fileSize = size;
                    regular.contents.narOffset = pos;
                }

                void operator()(std::string_view data) override {}
            } nmc{regRef, pos};

            func(nmc);
        }

        void createSymlink(const std::string & target) override
        {
            if (currentDst) {
                *currentDst = NarListing::Symlink{.target = target};
            } else {
                root = NarListing::Symlink{.target = target};
            }
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
