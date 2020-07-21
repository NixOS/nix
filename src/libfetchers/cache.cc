#include "cache.hh"
#include "sqlite.hh"
#include "sync.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

static const char * schema = R"sql(

create table if not exists Cache (
    input     text not null,
    info      text not null,
    path      text not null,
    immutable integer not null,
    timestamp integer not null,
    primary key (input)
);
)sql";

struct CacheImpl : Cache
{
    struct State
    {
        SQLite db;
        SQLiteStmt add, lookup;
    };

    Sync<State> _state;

    CacheImpl()
    {
        auto state(_state.lock());

        auto dbPath = getCacheDir() + "/nix/fetcher-cache-v1.sqlite";
        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->add.create(state->db,
            "insert or replace into Cache(input, info, path, immutable, timestamp) values (?, ?, ?, ?, ?)");

        state->lookup.create(state->db,
            "select info, path, immutable, timestamp from Cache where input = ?");
    }

    void add(
        ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePathDescriptor & storePathDesc,
        bool immutable) override
    {
        _state.lock()->add.use()
            (attrsToJson(inAttrs).dump())
            (attrsToJson(infoAttrs).dump())
            // FIXME should use JSON for store path descriptor
            (renderStorePathDescriptor(storePathDesc))
            (immutable)
            (time(0)).exec();
    }

    std::optional<std::pair<Attrs, StorePathDescriptor>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) override
    {
        if (auto res = lookupExpired(store, inAttrs)) {
            if (!res->expired)
                return std::make_pair(std::move(res->infoAttrs), std::move(res->storePath));
            debug("ignoring expired cache entry '%s'",
                attrsToJson(inAttrs).dump());
        }
        return {};
    }

    std::optional<Result> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) override
    {
        auto state(_state.lock());

        auto inAttrsJson = attrsToJson(inAttrs).dump();

        auto stmt(state->lookup.use()(inAttrsJson));
        if (!stmt.next()) {
            debug("did not find cache entry for '%s'", inAttrsJson);
            return {};
        }

        auto infoJson = stmt.getStr(0);
        auto storePathDesc = parseStorePathDescriptor(stmt.getStr(1));
        auto immutable = stmt.getInt(2) != 0;
        auto timestamp = stmt.getInt(3);
        auto storePath = store->makeFixedOutputPathFromCA(storePathDesc);

        store->addTempRoot(storePath);
        if (!store->isValidPath(storePath)) {
            // FIXME: we could try to substitute 'storePath'.
            debug("ignoring disappeared cache entry '%s'", inAttrsJson);
            return {};
        }

        debug("using cache entry '%s' -> '%s', '%s'",
            inAttrsJson, infoJson, store->printStorePath(storePath));

        return Result {
            .expired = !immutable && (settings.tarballTtl.get() == 0 || timestamp + settings.tarballTtl < time(0)),
            .infoAttrs = jsonToAttrs(nlohmann::json::parse(infoJson)),
            .storePath = std::move(storePathDesc)
        };
    }
};

ref<Cache> getCache()
{
    static auto cache = std::make_shared<CacheImpl>();
    return ref<Cache>(cache);
}

}
