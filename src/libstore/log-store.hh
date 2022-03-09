#pragma once

#include "store-api.hh"


namespace nix {

struct LogStore : public virtual Store
{
    inline static std::string operationName = "Build log storage and retrieval";

    /* Return the build log of the specified store path, if available,
       or null otherwise. */
    virtual std::optional<std::string> getBuildLog(const StorePath & path) = 0;

    virtual void addBuildLog(const StorePath & path, std::string_view log) = 0;

    static LogStore & require(Store & store);
};

}
