#pragma once

#include "ref.hh"
#include "nar-info.hh"
#include "realisation.hh"

namespace nix {

class NarInfoDiskCache
{
public:
    typedef enum { oValid, oInvalid, oUnknown } Outcome;

    virtual ~NarInfoDiskCache() { }

    virtual void createCache(const std::string & uri, const Path & storeDir,
        bool wantMassQuery, int priority) = 0;

    struct CacheInfo
    {
        bool wantMassQuery;
        int priority;
    };

    virtual std::optional<CacheInfo> cacheExists(const std::string & uri) = 0;

    virtual std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        const std::string & uri, const std::string & hashPart) = 0;

    virtual void upsertNarInfo(
        const std::string & uri, const std::string & hashPart,
        std::shared_ptr<const ValidPathInfo> info) = 0;

    virtual void upsertRealisation(
        const std::string & uri,
        const Realisation & realisation) = 0;
    virtual void upsertAbsentRealisation(
        const std::string & uri,
        const DrvOutput & id) = 0;
    virtual std::pair<Outcome, std::shared_ptr<Realisation>> lookupRealisation(
        const std::string & uri, const DrvOutput & id) = 0;
};

/* Return a singleton cache object that can be used concurrently by
   multiple threads. */
ref<NarInfoDiskCache> getNarInfoDiskCache();

}
