#include "cache.hh"
#include "users.hh"
#include "sqlite.hh"
#include "sync.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

static const char * schema = R"sql(

create table if not exists Cache (
    domain    text not null,
    key       text not null,
    value     text not null,
    timestamp integer not null,
    primary key (domain, key)
);
)sql";

// FIXME: we should periodically purge/nuke this cache to prevent it
// from growing too big.

struct CacheImpl : Cache
{
    struct State
    {
        SQLite db;
        SQLiteStmt upsert, lookup;
    };

    Sync<State> _state;

    CacheImpl()
    {
        auto state(_state.lock());

        auto dbPath = getCacheDir() + "/nix/fetcher-cache-v2.sqlite";
        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->upsert.create(state->db,
            "insert or replace into Cache(domain, key, value, timestamp) values (?, ?, ?, ?)");

        state->lookup.create(state->db,
            "select value, timestamp from Cache where domain = ? and key = ?");
    }

    void upsert(
        std::string_view domain,
        const Attrs & key,
        const Attrs & value) override
    {
        _state.lock()->upsert.use()
            (domain)
            (attrsToJSON(key).dump())
            (attrsToJSON(value).dump())
            (time(0)).exec();
    }

    std::optional<Attrs> lookup(
        std::string_view domain,
        const Attrs & key) override
    {
        if (auto res = lookupExpired(domain, key))
            return std::move(res->value);
        return {};
    }

    std::optional<Attrs> lookupWithTTL(
        std::string_view domain,
        const Attrs & key) override
    {
        if (auto res = lookupExpired(domain, key)) {
            if (!res->expired)
                return std::move(res->value);
            debug("ignoring expired cache entry '%s:%s'",
                domain, attrsToJSON(key).dump());
        }
        return {};
    }

    std::optional<Result> lookupExpired(
        std::string_view domain,
        const Attrs & key) override
    {
        auto state(_state.lock());

        auto keyJSON = attrsToJSON(key).dump();

        auto stmt(state->lookup.use()(domain)(keyJSON));
        if (!stmt.next()) {
            debug("did not find cache entry for '%s:%s'", domain, keyJSON);
            return {};
        }

        auto valueJSON = stmt.getStr(0);
        auto timestamp = stmt.getInt(1);

        debug("using cache entry '%s:%s' -> '%s'", domain, keyJSON, valueJSON);

        return Result {
            .expired = settings.tarballTtl.get() == 0 || timestamp + settings.tarballTtl < time(0),
            .value = jsonToAttrs(nlohmann::json::parse(valueJSON)),
        };
    }

    void upsert(
        std::string_view domain,
        Attrs key,
        Store & store,
        Attrs value,
        const StorePath & storePath)
    {
        /* Add the store prefix to the cache key to handle multiple
           store prefixes. */
        key.insert_or_assign("store", store.storeDir);

        value.insert_or_assign("storePath", (std::string) storePath.to_string());

        upsert(domain, key, value);
    }

    std::optional<ResultWithStorePath> lookupStorePath(
        std::string_view domain,
        Attrs key,
        Store & store) override
    {
        key.insert_or_assign("store", store.storeDir);

        auto res = lookupExpired(domain, key);
        if (!res) return std::nullopt;

        auto storePathS = getStrAttr(res->value, "storePath");
        res->value.erase("storePath");

        ResultWithStorePath res2(*res, StorePath(storePathS));

        store.addTempRoot(res2.storePath);
        if (!store.isValidPath(res2.storePath)) {
            // FIXME: we could try to substitute 'storePath'.
            debug("ignoring disappeared cache entry '%s' -> '%s'",
                attrsToJSON(key).dump(),
                store.printStorePath(res2.storePath));
            return std::nullopt;
        }

        debug("using cache entry '%s' -> '%s', '%s'",
            attrsToJSON(key).dump(),
            attrsToJSON(res2.value).dump(),
            store.printStorePath(res2.storePath));

        return res2;
    }

    std::optional<ResultWithStorePath> lookupStorePathWithTTL(
        std::string_view domain,
        Attrs key,
        Store & store) override
    {
        auto res = lookupStorePath(domain, std::move(key), store);
        return res && !res->expired ? res : std::nullopt;
    }
};

ref<Cache> getCache()
{
    static auto cache = std::make_shared<CacheImpl>();
    return ref<Cache>(cache);
}

}
