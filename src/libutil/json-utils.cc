#include "json-utils.hh"

namespace nix {

const nlohmann::json * get(const nlohmann::json & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

nlohmann::json * get(nlohmann::json & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

}
