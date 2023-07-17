#include "derived-path.hh"

namespace nix {

/**
 * A built derived path with hints in the form of optional concrete output paths.
 *
 * See 'BuiltPath' for more an explanation.
 */
struct BuiltPathBuilt {
    StorePath drvPath;
    std::map<std::string, StorePath> outputs;

    nlohmann::json toJSON(ref<Store> store) const;
    static BuiltPathBuilt parse(const Store & store, std::string_view);

    GENERATE_CMP(BuiltPathBuilt, me->drvPath, me->outputs);
};

using _BuiltPathRaw = std::variant<
    DerivedPath::Opaque,
    BuiltPathBuilt
>;

/**
 * A built path. Similar to a DerivedPath, but enriched with the corresponding
 * output path(s).
 */
struct BuiltPath : _BuiltPathRaw {
    using Raw = _BuiltPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = BuiltPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    StorePathSet outPaths() const;
    RealisedPath::Set toRealisedPaths(Store & store) const;

};

typedef std::vector<BuiltPath> BuiltPaths;

}
