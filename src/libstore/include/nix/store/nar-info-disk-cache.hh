#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/realisation.hh"

namespace nix {

struct SQLiteSettings;
struct NarInfoDiskCacheSettings;

struct NarInfoDiskCache
{
    using Settings = NarInfoDiskCacheSettings;

    const Settings & settings;

    NarInfoDiskCache(const Settings & settings)
        : settings(settings)
    {
    }

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

    /**
     * Return a singleton cache object that can be used concurrently by
     * multiple threads.
     *
     * @note the parameters are only used to initialize this the first time this is called.
     * In subsequent calls, these arguments are ignored.
     *
     * @todo Probably should instead create a memo table so multiple settings -> multiple instances,
     * but this is not yet a problem in practice.
     */
    static ref<NarInfoDiskCache> get(const Settings & settings, SQLiteSettings);

    static ref<NarInfoDiskCache> getTest(const Settings & settings, SQLiteSettings, std::filesystem::path dbPath);
};

} // namespace nix
