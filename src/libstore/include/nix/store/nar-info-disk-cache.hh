#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/realisation.hh"

namespace nix {

class Settings;

class NarInfoDiskCache
{
public:
    typedef enum { oValid, oInvalid, oUnknown } Outcome;

    virtual ~NarInfoDiskCache() {}

    virtual int createCache(const std::string & uri, const Path & storeDir, bool wantMassQuery, int priority) = 0;

    struct CacheInfo
    {
        int id;
        bool wantMassQuery;
        int priority;
    };

    virtual std::optional<CacheInfo> upToDateCacheExists(const std::string & uri) = 0;

    virtual std::pair<Outcome, std::shared_ptr<NarInfo>>
    lookupNarInfo(const std::string & uri, const std::string & hashPart) = 0;

    virtual void
    upsertNarInfo(const std::string & uri, const std::string & hashPart, std::shared_ptr<const ValidPathInfo> info) = 0;

    virtual void upsertRealisation(const std::string & uri, const Realisation & realisation) = 0;
    virtual void upsertAbsentRealisation(const std::string & uri, const DrvOutput & id) = 0;
    virtual std::pair<Outcome, std::shared_ptr<Realisation>>
    lookupRealisation(const std::string & uri, const DrvOutput & id) = 0;
};

/**
 * Return a singleton cache object that can be used concurrently by
 * multiple threads.
 *
 * @todo should use refined reference just with fields relevant to this,
 * not the whole global settings.
 */
ref<NarInfoDiskCache> getNarInfoDiskCache(const Settings & settings);

ref<NarInfoDiskCache> getTestNarInfoDiskCache(const Settings & settings, Path dbPath);

} // namespace nix
