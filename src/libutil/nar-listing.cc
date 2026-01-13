#include "nix/util/nar-listing.hh"

namespace nix {

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
