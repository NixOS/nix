#pragma once

#include "util.hh"
#include "path.hh"
#include "path.hh"

#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace nix {

class Store;

struct BuildableOpaque {
    StorePath path;

    nlohmann::json toJSON(ref<Store> store) const;
    std::string to_string(const Store & store) const;
    static BuildableOpaque parse(const Store & store, std::string_view);
};

template<typename Outputs>
struct BuildableForFromDrv {
    StorePath drvPath;
    Outputs outputs;

    nlohmann::json toJSON(ref<Store> store) const;
    std::string to_string(const Store & store) const;
    static BuildableForFromDrv<Outputs> parse(const Store & store, std::string_view);
};

template <typename Outputs>
using BuildableFor = std::variant<
    BuildableOpaque,
    BuildableForFromDrv<Outputs>
>;

typedef BuildableForFromDrv<std::set<std::string>> BuildableReqFromDrv;
typedef BuildableFor<std::set<std::string>> BuildableReq;

std::string to_string(const Store & store, const BuildableReq &);

BuildableReq parseBuildableReq(const Store & store, std::string_view);

typedef BuildableForFromDrv<std::map<std::string, std::optional<StorePath>>> BuildableFromDrv;
typedef BuildableFor<std::map<std::string, std::optional<StorePath>>> Buildable;

typedef std::vector<Buildable> Buildables;

nlohmann::json buildablesToJSON(const Buildables & buildables, ref<Store> store);

}
