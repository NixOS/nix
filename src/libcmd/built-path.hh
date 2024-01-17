#pragma once
///@file

#include "derived-path.hh"
#include "realisation.hh"

namespace nix {

struct SingleBuiltPath;

struct SingleBuiltPathBuilt {
    ref<SingleBuiltPath> drvPath;
    std::pair<std::string, StorePath> output;

    SingleDerivedPathBuilt discardOutputPath() const;

    std::string to_string(const StoreDirConfig & store) const;
    static SingleBuiltPathBuilt parse(const StoreDirConfig & store, std::string_view, std::string_view);
    nlohmann::json toJSON(const StoreDirConfig & store) const;

    DECLARE_CMP(SingleBuiltPathBuilt);
};

using _SingleBuiltPathRaw = std::variant<
    DerivedPathOpaque,
    SingleBuiltPathBuilt
>;

struct SingleBuiltPath : _SingleBuiltPathRaw {
    using Raw = _SingleBuiltPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = SingleBuiltPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    StorePath outPath() const;

    SingleDerivedPath discardOutputPath() const;

    static SingleBuiltPath parse(const StoreDirConfig & store, std::string_view);
    nlohmann::json toJSON(const StoreDirConfig & store) const;
};

static inline ref<SingleBuiltPath> staticDrv(StorePath drvPath)
{
    return make_ref<SingleBuiltPath>(SingleBuiltPath::Opaque { drvPath });
}

/**
 * A built derived path with hints in the form of optional concrete output paths.
 *
 * See 'BuiltPath' for more an explanation.
 */
struct BuiltPathBuilt {
    ref<SingleBuiltPath> drvPath;
    std::map<std::string, StorePath> outputs;

    std::string to_string(const StoreDirConfig & store) const;
    static BuiltPathBuilt parse(const StoreDirConfig & store, std::string_view, std::string_view);
    nlohmann::json toJSON(const StoreDirConfig & store) const;

    DECLARE_CMP(BuiltPathBuilt);
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

    nlohmann::json toJSON(const StoreDirConfig & store) const;
};

typedef std::vector<BuiltPath> BuiltPaths;

}
