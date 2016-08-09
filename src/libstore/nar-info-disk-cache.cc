#include "nar-info-disk-cache.hh"
#include "sync.hh"
#include "sqlite.hh"

#include <sqlite3.h>

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
    timestamp        integer not null,
    present          integer not null,
    primary key (cache, hashPart),
    foreign key (cache) references BinaryCaches(id) on delete cascade
);

create table if not exists NARExistence (
    cache            integer not null,
    storePath        text not null,
    exist            integer not null,
    timestamp        integer not null,
    primary key (cache, storePath),
    foreign key (cache) references BinaryCaches(id) on delete cascade
);

)sql";

class NarInfoDiskCacheImpl : public NarInfoDiskCache
{
public:

    /* How long negative lookups are valid. */
    const int ttlNegative = 3600;

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
        SQLiteStmt insertCache, queryCache, insertNAR, insertMissingNAR, queryNAR;
        std::map<std::string, Cache> caches;
    };

    Sync<State> _state;

    NarInfoDiskCacheImpl()
    {
        auto state(_state.lock());

        Path dbPath = getCacheDir() + "/nix/binary-cache-v5.sqlite";
        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);

        if (sqlite3_busy_timeout(state->db, 60 * 60 * 1000) != SQLITE_OK)
            throwSQLiteError(state->db, "setting timeout");

        // We can always reproduce the cache.
        state->db.exec("pragma synchronous = off");
        state->db.exec("pragma main.journal_mode = truncate");

        state->db.exec(schema);

        state->insertCache.create(state->db,
            "insert or replace into BinaryCaches(url, timestamp, storeDir, wantMassQuery, priority) values (?, ?, ?, ?, ?)");

        state->queryCache.create(state->db,
            "select id, storeDir, wantMassQuery, priority from BinaryCaches where url = ?");

        state->insertNAR.create(state->db,
            "insert or replace into NARs(cache, hashPart, namePart, url, compression, fileHash, fileSize, narHash, "
            "narSize, refs, deriver, sigs, timestamp, present) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)");

        state->insertMissingNAR.create(state->db,
            "insert or replace into NARs(cache, hashPart, timestamp, present) values (?, ?, ?, 0)");

        state->queryNAR.create(state->db,
            "select * from NARs where cache = ? and hashPart = ?");
    }

    Cache & getCache(State & state, const std::string & uri)
    {
        auto i = state.caches.find(uri);
        if (i == state.caches.end()) abort();
        return i->second;
    }

    void createCache(const std::string & uri, const Path & storeDir, bool wantMassQuery, int priority) override
    {
        auto state(_state.lock());

        // FIXME: race

        state->insertCache.use()(uri)(time(0))(storeDir)(wantMassQuery)(priority).exec();
        assert(sqlite3_changes(state->db) == 1);
        state->caches[uri] = Cache{(int) sqlite3_last_insert_rowid(state->db), storeDir, wantMassQuery, priority};
    }

    bool cacheExists(const std::string & uri,
        bool & wantMassQuery, int & priority) override
    {
        auto state(_state.lock());

        auto i = state->caches.find(uri);
        if (i == state->caches.end()) {
            auto queryCache(state->queryCache.use()(uri));
            if (!queryCache.next()) return false;
            state->caches.emplace(uri,
                Cache{(int) queryCache.getInt(0), queryCache.getStr(1), queryCache.getInt(2) != 0, (int) queryCache.getInt(3)});
        }

        auto & cache(getCache(*state, uri));

        wantMassQuery = cache.wantMassQuery;
        priority = cache.priority;

        return true;
    }

    std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        const std::string & uri, const std::string & hashPart) override
    {
        auto state(_state.lock());

        auto & cache(getCache(*state, uri));

        auto queryNAR(state->queryNAR.use()(cache.id)(hashPart));

        if (!queryNAR.next())
            // FIXME: check NARExistence
            return {oUnknown, 0};

        if (!queryNAR.getInt(13))
            return {oInvalid, 0};

        auto narInfo = make_ref<NarInfo>();

        // FIXME: implement TTL.

        auto namePart = queryNAR.getStr(2);
        narInfo->path = cache.storeDir + "/" +
            hashPart + (namePart.empty() ? "" : "-" + namePart);
        narInfo->url = queryNAR.getStr(3);
        narInfo->compression = queryNAR.getStr(4);
        if (!queryNAR.isNull(5))
            narInfo->fileHash = parseHash(queryNAR.getStr(5));
        narInfo->fileSize = queryNAR.getInt(6);
        narInfo->narHash = parseHash(queryNAR.getStr(7));
        narInfo->narSize = queryNAR.getInt(8);
        for (auto & r : tokenizeString<Strings>(queryNAR.getStr(9), " "))
            narInfo->references.insert(cache.storeDir + "/" + r);
        if (!queryNAR.isNull(10))
            narInfo->deriver = cache.storeDir + "/" + queryNAR.getStr(10);
        for (auto & sig : tokenizeString<Strings>(queryNAR.getStr(11), " "))
            narInfo->sigs.insert(sig);

        return {oValid, narInfo};
    }

    void upsertNarInfo(
        const std::string & uri, const std::string & hashPart,
        std::shared_ptr<ValidPathInfo> info) override
    {
        auto state(_state.lock());

        auto & cache(getCache(*state, uri));

        if (info) {

            auto narInfo = std::dynamic_pointer_cast<NarInfo>(info);

            assert(hashPart == storePathToHash(info->path));

            state->insertNAR.use()
                (cache.id)
                (hashPart)
                (storePathToName(info->path))
                (narInfo ? narInfo->url : "", narInfo != 0)
                (narInfo ? narInfo->compression : "", narInfo != 0)
                (narInfo && narInfo->fileHash ? narInfo->fileHash.to_string() : "", narInfo && narInfo->fileHash)
                (narInfo ? narInfo->fileSize : 0, narInfo != 0 && narInfo->fileSize)
                (info->narHash.to_string())
                (info->narSize)
                (concatStringsSep(" ", info->shortRefs()))
                (info->deriver != "" ? baseNameOf(info->deriver) : "", info->deriver != "")
                (concatStringsSep(" ", info->sigs))
                (time(0)).exec();

        } else {
            state->insertMissingNAR.use()
                (cache.id)
                (hashPart)
                (time(0)).exec();
        }
    }
};

ref<NarInfoDiskCache> getNarInfoDiskCache()
{
    static Sync<std::shared_ptr<NarInfoDiskCache>> cache;

    auto cache_(cache.lock());
    if (!*cache_) *cache_ = std::make_shared<NarInfoDiskCacheImpl>();
    return ref<NarInfoDiskCache>(*cache_);
}

}
