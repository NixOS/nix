#include "tree-info.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

StorePath TreeInfo::computeStorePath(Store & store) const
{
    assert(narHash);
    return store.makeFixedOutputPath(true, narHash, "source");
}

TreeInfo TreeInfo::fromJson(const nlohmann::json & json)
{
    TreeInfo info;

    auto i = json.find("info");
    if (i != json.end()) {
        const nlohmann::json & i2(*i);

        auto j = i2.find("narHash");
        if (j != i2.end())
            info.narHash = Hash((std::string) *j);
        else
            throw Error("attribute 'narHash' missing in lock file");

        j = i2.find("revCount");
        if (j != i2.end())
            info.revCount = *j;

        j = i2.find("lastModified");
        if (j != i2.end())
            info.lastModified = *j;

        return info;
    }

    i = json.find("narHash");
    if (i != json.end()) {
        info.narHash = Hash((std::string) *i);
        return info;
    }

    throw Error("attribute 'info' missing in lock file");
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
