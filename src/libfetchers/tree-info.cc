#include "tree-info.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

StorePath TreeInfo::computeStorePath(Store & store) const
{
    assert(narHash);
    return store.makeFixedOutputPath(true, narHash, "source");
}

nlohmann::json TreeInfo::toJson() const
{
    nlohmann::json json;
    assert(narHash);
    json["narHash"] = narHash.to_string(SRI);
    if (revCount)
        json["revCount"] = *revCount;
    if (lastModified)
        json["lastModified"] = *lastModified;
    return json;
}

}
