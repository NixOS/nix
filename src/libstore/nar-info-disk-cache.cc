#include "nar-info-disk-cache.hh"
#include "sync.hh"
#include "sqlite.hh"
#include "globals.hh"

#include <sqlite3.h>
#include <nlohmann/json.hpp>

namespace nix {

static const char * schema = R"sql(

create table if not exists BinaryCaches (
    id        integer primary key autoincrement not null,
    url       text unique not null,
    timestamp integer not null,
    storeDir  text not null,
    wantMassQuery integer not null,
    priority  integer not null
);

create table if not exists NARs (
    cache            integer not null,
    hashPart         text not null,
    namePart         text,
    url              text,
    compression      text,
    fileHash         text,
    fileSize         integer,
    narHash          text,
    narSize          integer,
    refs             text,
    deriver          text,
    sigs             text,
    ca               text,
    timestamp        integer not null,
    present          integer not null,
    primary key (cache, hashPart),
    foreign key (cache) references BinaryCaches(id) on delete cascade
);

create table if not exists Realisations (
    cache integer not null,
    outputId text not null,
    content blob, -- Json serialisation of the realisation, or null if the realisation is absent
    timestamp        integer not null,
    primary key (cache, outputId),
    foreign key (cache) references BinaryCaches(id) on delete cascade
);

create table if not exists LastPurge (
    dummy            text primary key,
    value            integer
);

)sql";

class NarInfoDiskCacheImpl : public NarInfoDiskCache
{
public:

    /* How often to purge expired entries from the cache. */
    const int purgeInterval = 24 * 3600;

    struct Cache
    {
        int id;
        Path storeDir;
        bool wantMassQuery;
        int priority;
    };

    struct State
    {
        SQLite db;
        SQLiteStmt insertCache, queryCache, insertNAR, insertMissingNAR,
            queryNAR, insertRealisation, insertMissingRealisation,
            queryRealisation, purgeCache;
        std::map<std::string, Cache> caches;
    };

    Sync<State> _state;

    NarInfoDiskCacheImpl()
    {
        auto state(_state.lock());

        Path dbPath = getCacheDir() + "/nix/binary-cache-v6.sqlite";
        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);

        state->db.isCache();

        state->db.exec(schema);

        state->insertCache.create(state->db,
            "insert or replace into BinaryCaches(url, timestamp, storeDir, wantMassQuery, priority) values (?, ?, ?, ?, ?)");

        state->queryCache.create(state->db,
            "select id, storeDir, wantMassQuery, priority from BinaryCaches where url = ?");

        state->insertNAR.create(state->db,
            "insert or replace into NARs(cache, hashPart, namePart, url, compression, fileHash, fileSize, narHash, "
            "narSize, refs, deriver, sigs, ca, timestamp, present) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)");

        state->insertMissingNAR.create(state->db,
            "insert or replace into NARs(cache, hashPart, timestamp, present) values (?, ?, ?, 0)");

        state->queryNAR.create(state->db,
            "select present, namePart, url, compression, fileHash, fileSize, narHash, narSize, refs, deriver, sigs, ca from NARs where cache = ? and hashPart = ? and ((present = 0 and timestamp > ?) or (present = 1 and timestamp > ?))");

        state->insertRealisation.create(state->db,
            R"(
                insert or replace into Realisations(cache, outputId, content, timestamp)
                    values (?, ?, ?, ?)
            )");

        state->insertMissingRealisation.create(state->db,
            R"(
                insert or replace into Realisations(cache, outputId, timestamp)
                    values (?, ?, ?)
            )");

        state->queryRealisation.create(state->db,
            R"(
                select content from Realisations
                    where cache = ? and outputId = ?  and
                        ((content is null and timestamp > ?) or
                         (content is not null and timestamp > ?))
            )");

        /* Periodically purge expired entries from the database. */
        retrySQLite<void>([&]() {
            auto now = time(0);

            SQLiteStmt queryLastPurge(state->db, "select value from LastPurge");
            auto queryLastPurge_(queryLastPurge.use());

            if (!queryLastPurge_.next() || queryLastPurge_.getInt(0) < now - purgeInterval) {
                SQLiteStmt(state->db,
                    "delete from NARs where ((present = 0 and timestamp < ?) or (present = 1 and timestamp < ?))")
                    .use()
                    // Use a minimum TTL to prevent --refresh from
                    // nuking the entire disk cache.
                    (now - std::max(settings.ttlNegativeNarInfoCache.get(), 3600U))
                    (now - std::max(settings.ttlPositiveNarInfoCache.get(), 30 * 24 * 3600U))
                    .exec();

                debug("deleted %d entries from the NAR info disk cache", sqlite3_changes(state->db));

                SQLiteStmt(state->db,
                    "insert or replace into LastPurge(dummy, value) values ('', ?)")
                    .use()(now).exec();
            }
        });
    }

    Cache & getCache(State & state, const std::string & uri)
    {
        auto i = state.caches.find(uri);
        if (i == state.caches.end()) abort();
        return i->second;
    }

    void createCache(const std::string & uri, const Path & storeDir, bool wantMassQuery, int priority) override
    {
        retrySQLite<void>([&]() {
            auto state(_state.lock());

            // FIXME: race

            state->insertCache.use()(uri)(time(0))(storeDir)(wantMassQuery)(priority).exec();
            assert(sqlite3_changes(state->db) == 1);
            state->caches[uri] = Cache{(int) sqlite3_last_insert_rowid(state->db), storeDir, wantMassQuery, priority};
        });
    }

    std::optional<CacheInfo> cacheExists(const std::string & uri) override
    {
        return retrySQLite<std::optional<CacheInfo>>([&]() -> std::optional<CacheInfo> {
            auto state(_state.lock());

            auto i = state->caches.find(uri);
            if (i == state->caches.end()) {
                auto queryCache(state->queryCache.use()(uri));
                if (!queryCache.next())
                    return std::nullopt;
                state->caches.emplace(uri,
                    Cache{(int) queryCache.getInt(0), queryCache.getStr(1), queryCache.getInt(2) != 0, (int) queryCache.getInt(3)});
            }

            auto & cache(getCache(*state, uri));

            return CacheInfo {
                .wantMassQuery = cache.wantMassQuery,
                .priority = cache.priority
            };
        });
    }

    std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        const std::string & uri, const std::string & hashPart) override
    {
        return retrySQLite<std::pair<Outcome, std::shared_ptr<NarInfo>>>(
            [&]() -> std::pair<Outcome, std::shared_ptr<NarInfo>> {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            auto now = time(0);

            auto queryNAR(state->queryNAR.use()
                (cache.id)
                (hashPart)
                (now - settings.ttlNegativeNarInfoCache)
                (now - settings.ttlPositiveNarInfoCache));

            if (!queryNAR.next())
                return {oUnknown, 0};

            if (!queryNAR.getInt(0))
                return {oInvalid, 0};

            auto namePart = queryNAR.getStr(1);
            auto narInfo = make_ref<NarInfo>(
                StorePath(hashPart + "-" + namePart),
                Hash::parseAnyPrefixed(queryNAR.getStr(6)));
            narInfo->url = queryNAR.getStr(2);
            narInfo->compression = queryNAR.getStr(3);
            if (!queryNAR.isNull(4))
                narInfo->fileHash = Hash::parseAnyPrefixed(queryNAR.getStr(4));
            narInfo->fileSize = queryNAR.getInt(5);
            narInfo->narSize = queryNAR.getInt(7);
            for (auto & r : tokenizeString<Strings>(queryNAR.getStr(8), " "))
                narInfo->insertReferencePossiblyToSelf(StorePath(r));
            if (!queryNAR.isNull(9))
                narInfo->deriver = StorePath(queryNAR.getStr(9));
            for (auto & sig : tokenizeString<Strings>(queryNAR.getStr(10), " "))
                narInfo->sigs.insert(sig);
            narInfo->ca = parseContentAddressOpt(queryNAR.getStr(11));

            return {oValid, narInfo};
        });
    }

    std::pair<Outcome, std::shared_ptr<Realisation>> lookupRealisation(
        const std::string & uri, const DrvOutput & id) override
    {
        return retrySQLite<std::pair<Outcome, std::shared_ptr<Realisation>>>(
            [&]() -> std::pair<Outcome, std::shared_ptr<Realisation>> {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            auto now = time(0);

            auto queryRealisation(state->queryRealisation.use()
                (cache.id)
                (id.to_string())
                (now - settings.ttlNegativeNarInfoCache)
                (now - settings.ttlPositiveNarInfoCache));

            if (!queryRealisation.next())
                return {oUnknown, 0};

            if (queryRealisation.isNull(0))
                return {oInvalid, 0};

            auto realisation =
                std::make_shared<Realisation>(Realisation::fromJSON(
                    nlohmann::json::parse(queryRealisation.getStr(0)),
                    "Local disk cache"));

            return {oValid, realisation};
        });
    }

    void upsertNarInfo(
        const std::string & uri, const std::string & hashPart,
        std::shared_ptr<const ValidPathInfo> info) override
    {
        retrySQLite<void>([&]() {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            if (info) {

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

                //assert(hashPart == storePathToHash(info->path));

                state->insertNAR.use()
                    (cache.id)
                    (hashPart)
                    (std::string(info->path.name()))
                    (narInfo ? narInfo->url : "", narInfo != 0)
                    (narInfo ? narInfo->compression : "", narInfo != 0)
                    (narInfo && narInfo->fileHash ? narInfo->fileHash->to_string(Base32, true) : "", narInfo && narInfo->fileHash)
                    (narInfo ? narInfo->fileSize : 0, narInfo != 0 && narInfo->fileSize)
                    (info->narHash.to_string(Base32, true))
                    (info->narSize)
                    (concatStringsSep(" ", info->shortRefs()))
                    (info->deriver ? std::string(info->deriver->to_string()) : "", (bool) info->deriver)
                    (concatStringsSep(" ", info->sigs))
                    (renderContentAddress(info->ca))
                    (time(0)).exec();

            } else {
                state->insertMissingNAR.use()
                    (cache.id)
                    (hashPart)
                    (time(0)).exec();
            }
        });
    }

    void upsertRealisation(
        const std::string & uri,
        const Realisation & realisation) override
    {
        retrySQLite<void>([&]() {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            state->insertRealisation.use()
                (cache.id)
                (realisation.id.to_string())
                (realisation.toJSON().dump())
                (time(0)).exec();
        });

    }

    virtual void upsertAbsentRealisation(
        const std::string & uri,
        const DrvOutput & id) override
    {
        retrySQLite<void>([&]() {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));
            state->insertMissingRealisation.use()
                (cache.id)
                (id.to_string())
                (time(0)).exec();
        });
    }
};

ref<NarInfoDiskCache> getNarInfoDiskCache()
{
    static ref<NarInfoDiskCache> cache = make_ref<NarInfoDiskCacheImpl>();
    return cache;
}

}
