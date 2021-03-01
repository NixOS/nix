#pragma once

#include "util.hh"
#include "path.hh"

#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace nix {

class Store;

struct BuildableOpaque {
    StorePath path;
    nlohmann::json toJSON(ref<Store> store) const;
};

struct BuildableFromDrv {
    StorePath drvPath;
    std::map<std::string, std::optional<StorePath>> outputs;
    nlohmann::json toJSON(ref<Store> store) const;
};

typedef std::variant<
    BuildableOpaque,
    BuildableFromDrv
> Buildable;

typedef std::vector<Buildable> Buildables;

nlohmann::json buildablesToJSON(const Buildables & buildables, ref<Store> store);

}
