#pragma once

#include "util.hh"
#include "path.hh"

#include <optional>
#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix {

class Store;

struct BuildableOpaque {
    StorePath path;

    nlohmann::json toJSON(ref<Store> store) const;
    std::string to_string(const Store & store) const;
    static BuildableOpaque parse(const Store & store, std::string_view);
};

struct BuildableReqFromDrv {
    StorePath drvPath;
    std::set<std::string> outputs;

    std::string to_string(const Store & store) const;
    static BuildableReqFromDrv parse(const Store & store, std::string_view);
};

using _BuildableReqRaw = std::variant<
    BuildableOpaque,
    BuildableReqFromDrv
>;

struct BuildableReq : _BuildableReqRaw {
    using Raw = _BuildableReqRaw;
    using Raw::Raw;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    std::string to_string(const Store & store) const;
    static BuildableReq parse(const Store & store, std::string_view);
};

struct BuildableFromDrv {
    StorePath drvPath;
    std::map<std::string, std::optional<StorePath>> outputs;

    nlohmann::json toJSON(ref<Store> store) const;
    static BuildableFromDrv parse(const Store & store, std::string_view);
};

using Buildable = std::variant<
    BuildableOpaque,
    BuildableFromDrv
>;

typedef std::vector<Buildable> Buildables;

nlohmann::json buildablesToJSON(const Buildables & buildables, ref<Store> store);

}
