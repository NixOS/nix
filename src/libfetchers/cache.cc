#include "nix/fetchers/cache.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/users.hh"
#include "nix/store/sqlite.hh"
#include "nix/util/sync.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"

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

        auto dbPath = getCacheDir() + "/fetcher-cache-v4.sqlite";
        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->upsert.create(
            state->db, "insert or replace into Cache(domain, key, value, timestamp) values (?, ?, ?, ?)");

        state->lookup.create(state->db, "select value, timestamp from Cache where domain = ? and key = ?");
    }

    void upsert(const Key & key, const Attrs & value) override
    {
        _state.lock()
            ->upsert.use()(key.first)(attrsToJSON(key.second).dump())(attrsToJSON(value).dump())(time(0))
            .exec();
    }

    std::optional<Attrs> lookup(const Key & key) override
    {
        if (auto res = lookupExpired(key))
            return std::move(res->value);
        return {};
    }

    std::optional<Attrs> lookupWithTTL(const Key & key) override
    {
        if (auto res = lookupExpired(key)) {
            if (!res->expired)
                return std::move(res->value);
            debug("ignoring expired cache entry '%s:%s'", key.first, attrsToJSON(key.second).dump());
        }
        return {};
    }

    std::optional<Result> lookupExpired(const Key & key) override
    {
        auto state(_state.lock());

        auto keyJSON = attrsToJSON(key.second).dump();

        auto stmt(state->lookup.use()(key.first)(keyJSON));
        if (!stmt.next()) {
            debug("did not find cache entry for '%s:%s'", key.first, keyJSON);
            return {};
        }

        auto valueJSON = stmt.getStr(0);
        auto timestamp = stmt.getInt(1);

        debug("using cache entry '%s:%s' -> '%s'", key.first, keyJSON, valueJSON);

        return Result{
            .expired = settings.tarballTtl.get() == 0 || timestamp + settings.tarballTtl < time(0),
            .value = jsonToAttrs(nlohmann::json::parse(valueJSON)),
        };
    }

    void upsert(Key key, Store & store, Attrs value, const StorePath & storePath) override
    {
        /* Add the store prefix to the cache key to handle multiple
           store prefixes. */
        key.second.insert_or_assign("store", store.storeDir);

        value.insert_or_assign("storePath", (std::string) storePath.to_string());

        upsert(key, value);
    }

    std::optional<ResultWithStorePath> lookupStorePath(Key key, Store & store) override
    {
        key.second.insert_or_assign("store", store.storeDir);

        auto res = lookupExpired(key);
        if (!res)
            return std::nullopt;

        auto storePathS = getStrAttr(res->value, "storePath");
        res->value.erase("storePath");

        ResultWithStorePath res2(*res, StorePath(storePathS));

        store.addTempRoot(res2.storePath);
        if (!store.isValidPath(res2.storePath)) {
            // FIXME: we could try to substitute 'storePath'.
            debug(
                "ignoring disappeared cache entry '%s:%s' -> '%s'",
                key.first,
                attrsToJSON(key.second).dump(),
                store.printStorePath(res2.storePath));
            return std::nullopt;
        }

        debug(
            "using cache entry '%s:%s' -> '%s', '%s'",
            key.first,
            attrsToJSON(key.second).dump(),
            attrsToJSON(res2.value).dump(),
            store.printStorePath(res2.storePath));

        return res2;
    }

    std::optional<ResultWithStorePath> lookupStorePathWithTTL(Key key, Store & store) override
    {
        auto res = lookupStorePath(std::move(key), store);
        return res && !res->expired ? res : std::nullopt;
    }
};

ref<Cache> Settings::getCache() const
{
    auto cache(_cache.lock());
    if (!*cache)
        *cache = std::make_shared<CacheImpl>();
    return ref<Cache>(*cache);
}

} // namespace nix::fetchers
