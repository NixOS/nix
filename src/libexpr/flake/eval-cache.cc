#include "eval-cache.hh"
#include "sqlite.hh"
#include "eval.hh"

#include <set>

namespace nix::flake {

static const char * schema = R"sql(

create table if not exists Fingerprints (
    fingerprint blob primary key not null,
    timestamp   integer not null
);

create table if not exists Attributes (
    fingerprint blob not null,
    attrPath    text not null,
    type        integer,
    value       text,
    primary key (fingerprint, attrPath),
    foreign key (fingerprint) references Fingerprints(fingerprint) on delete cascade
);
)sql";

struct EvalCache::State
{
    SQLite db;
    SQLiteStmt insertFingerprint;
    SQLiteStmt insertAttribute;
    SQLiteStmt queryAttribute;
    std::set<Fingerprint> fingerprints;
};

EvalCache::EvalCache()
    : _state(std::make_unique<Sync<State>>())
{
    auto state(_state->lock());

    Path dbPath = getCacheDir() + "/nix/eval-cache-v1.sqlite";
    createDirs(dirOf(dbPath));

    state->db = SQLite(dbPath);
    state->db.isCache();
    state->db.exec(schema);

    state->insertFingerprint.create(state->db,
        "insert or ignore into Fingerprints(fingerprint, timestamp) values (?, ?)");

    state->insertAttribute.create(state->db,
        "insert or replace into Attributes(fingerprint, attrPath, type, value) values (?, ?, ?, ?)");

    state->queryAttribute.create(state->db,
        "select type, value from Attributes where fingerprint = ? and attrPath = ?");
}

enum ValueType {
    Derivation = 1,
};

void EvalCache::addDerivation(
    const Fingerprint & fingerprint,
    const std::string & attrPath,
    const Derivation & drv)
{
    if (!evalSettings.pureEval) return;

    auto state(_state->lock());

    if (state->fingerprints.insert(fingerprint).second)
        // FIXME: update timestamp
        state->insertFingerprint.use()
            (fingerprint.hash, fingerprint.hashSize)
            (time(0)).exec();

    state->insertAttribute.use()
        (fingerprint.hash, fingerprint.hashSize)
        (attrPath)
        (ValueType::Derivation)
        (drv.drvPath + " " + drv.outPath + " " + drv.outputName).exec();
}

std::optional<EvalCache::Derivation> EvalCache::getDerivation(
    const Fingerprint & fingerprint,
    const std::string & attrPath)
{
    if (!evalSettings.pureEval) return {};

    auto state(_state->lock());

    auto queryAttribute(state->queryAttribute.use()
        (fingerprint.hash, fingerprint.hashSize)
        (attrPath));
    if (!queryAttribute.next()) return {};

    // FIXME: handle negative results

    auto type = (ValueType) queryAttribute.getInt(0);
    auto s = queryAttribute.getStr(1);

    if (type != ValueType::Derivation) return {};

    auto ss = tokenizeString<std::vector<std::string>>(s, " ");

    debug("evaluation cache hit for '%s'", attrPath);

    return Derivation { ss[0], ss[1], ss[2] };
}

EvalCache & EvalCache::singleton()
{
    static std::unique_ptr<EvalCache> evalCache(new EvalCache());
    return *evalCache;
}

}
