#pragma once

#include "util.hh"
#include "path.hh"
#include "path.hh"

#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace nix {

class Store;

struct DerivedPathOpaque {
    StorePath path;

    nlohmann::json toJSON(ref<Store> store) const;
    std::string to_string(const Store & store) const;
    static DerivedPathOpaque parse(const Store & store, std::string_view);
};

struct DerivedPathBuilt {
    StorePath drvPath;
    std::set<std::string> outputs;

    std::string to_string(const Store & store) const;
    static DerivedPathBuilt parse(const Store & store, std::string_view);
};

using _DerivedPathRaw = std::variant<
    DerivedPathOpaque,
    DerivedPathBuilt
>;

struct DerivedPath : _DerivedPathRaw {
    using Raw = _DerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    std::string to_string(const Store & store) const;
    static DerivedPath parse(const Store & store, std::string_view);
};

struct DerivedPathWithHintsBuilt {
    StorePath drvPath;
    std::map<std::string, std::optional<StorePath>> outputs;

    nlohmann::json toJSON(ref<Store> store) const;
    static DerivedPathWithHintsBuilt parse(const Store & store, std::string_view);
};

using _DerivedPathWithHintsRaw = std::variant<
    DerivedPath::Opaque,
    DerivedPathWithHintsBuilt
>;

struct DerivedPathWithHints : _DerivedPathWithHintsRaw {
    using Raw = _DerivedPathWithHintsRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathWithHintsBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

};

typedef std::vector<DerivedPathWithHints> DerivedPathsWithHints;

nlohmann::json derivedPathsWithHintsToJSON(const DerivedPathsWithHints & buildables, ref<Store> store);

}
