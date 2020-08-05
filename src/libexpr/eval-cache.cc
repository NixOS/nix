#include "eval-cache.hh"
#include "sqlite.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "store-api.hh"

namespace nix::eval_cache {

static const char * schema = R"sql(
create table if not exists Attributes (
    parent      integer not null,
    name        text,
    type        integer not null,
    value       text,
    context     text,
    primary key (parent, name)
);
)sql";

struct AttrDb
{
    std::atomic_bool failed{false};

    struct State
    {
        SQLite db;
        SQLiteStmt insertAttribute;
        SQLiteStmt insertAttributeWithContext;
        SQLiteStmt queryAttribute;
        SQLiteStmt queryAttributes;
        std::unique_ptr<SQLiteTxn> txn;
    };

    std::unique_ptr<Sync<State>> _state;

    AttrDb(const Hash & fingerprint)
        : _state(std::make_unique<Sync<State>>())
    {
        auto state(_state->lock());

        Path cacheDir = getCacheDir() + "/nix/eval-cache-v2";
        createDirs(cacheDir);

        Path dbPath = cacheDir + "/" + fingerprint.to_string(Base16, false) + ".sqlite";

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->insertAttribute.create(state->db,
            "insert or replace into Attributes(parent, name, type, value) values (?, ?, ?, ?)");

        state->insertAttributeWithContext.create(state->db,
            "insert or replace into Attributes(parent, name, type, value, context) values (?, ?, ?, ?, ?)");

        state->queryAttribute.create(state->db,
            "select rowid, type, value, context from Attributes where parent = ? and name = ?");

        state->queryAttributes.create(state->db,
            "select name from Attributes where parent = ?");

        state->txn = std::make_unique<SQLiteTxn>(state->db);
    }

    ~AttrDb()
    {
        try {
            auto state(_state->lock());
            if (!failed)
                state->txn->commit();
            state->txn.reset();
        } catch (...) {
            ignoreException();
        }
    }

    template<typename F>
    AttrId doSQLite(F && fun)
    {
        if (failed) return 0;
        try {
            return fun();
        } catch (SQLiteError &) {
            ignoreException();
            failed = true;
            return 0;
        }
    }

    AttrId setAttrs(
        AttrKey key,
        const std::vector<Symbol> & attrs)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::FullAttrs)
                (0, false).exec();

            AttrId rowId = state->db.getLastInsertedRowId();
            assert(rowId);

            for (auto & attr : attrs)
                state->insertAttribute.use()
                    (rowId)
                    (attr)
                    (AttrType::Placeholder)
                    (0, false).exec();

            return rowId;
        });
    }

    AttrId setString(
        AttrKey key,
        std::string_view s,
        const char * * context = nullptr)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            if (context) {
                std::string ctx;
                for (const char * * p = context; *p; ++p) {
                    if (p != context) ctx.push_back(' ');
                    ctx.append(*p);
                }
                state->insertAttributeWithContext.use()
                    (key.first)
                    (key.second)
                    (AttrType::String)
                    (s)
                    (ctx).exec();
            } else {
                state->insertAttribute.use()
                    (key.first)
                    (key.second)
                    (AttrType::String)
                (s).exec();
            }

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setBool(
        AttrKey key,
        bool b)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Bool)
                (b ? 1 : 0).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setPlaceholder(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Placeholder)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setMissing(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Missing)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setMisc(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Misc)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setFailed(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Failed)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    std::optional<std::pair<AttrId, AttrValue>> getAttr(
        AttrKey key,
        SymbolTable & symbols)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(key.second));
        if (!queryAttribute.next()) return {};

        auto rowId = (AttrType) queryAttribute.getInt(0);
        auto type = (AttrType) queryAttribute.getInt(1);

        switch (type) {
            case AttrType::Placeholder:
                return {{rowId, placeholder_t()}};
            case AttrType::FullAttrs: {
                // FIXME: expensive, should separate this out.
                std::vector<Symbol> attrs;
                auto queryAttributes(state->queryAttributes.use()(rowId));
                while (queryAttributes.next())
                    attrs.push_back(symbols.create(queryAttributes.getStr(0)));
                return {{rowId, attrs}};
            }
            case AttrType::String: {
                std::vector<std::pair<Path, std::string>> context;
                if (!queryAttribute.isNull(3))
                    for (auto & s : tokenizeString<std::vector<std::string>>(queryAttribute.getStr(3), ";"))
                        context.push_back(decodeContext(s));
                return {{rowId, string_t{queryAttribute.getStr(2), context}}};
            }
            case AttrType::Bool:
                return {{rowId, queryAttribute.getInt(2) != 0}};
            case AttrType::Missing:
                return {{rowId, missing_t()}};
            case AttrType::Misc:
                return {{rowId, misc_t()}};
            case AttrType::Failed:
                return {{rowId, failed_t()}};
            default:
                throw Error("unexpected type in evaluation cache");
        }
    }
};

static std::shared_ptr<AttrDb> makeAttrDb(const Hash & fingerprint)
{
    try {
        return std::make_shared<AttrDb>(fingerprint);
    } catch (SQLiteError &) {
        ignoreException();
        return nullptr;
    }
}

EvalCache::EvalCache(
    std::optional<std::reference_wrapper<const Hash>> useCache,
    EvalState & state,
    RootLoader rootLoader)
    : db(useCache ? makeAttrDb(*useCache) : nullptr)
    , state(state)
    , rootLoader(rootLoader)
{
}

Value * EvalCache::getRootValue()
{
    if (!value) {
        debug("getting root value");
        value = allocRootValue(rootLoader());
    }
    return *value;
}

std::shared_ptr<AttrCursor> EvalCache::getRoot()
{
    return std::make_shared<AttrCursor>(ref(shared_from_this()), std::nullopt);
}

AttrCursor::AttrCursor(
    ref<EvalCache> root,
    Parent parent,
    Value * value,
    std::optional<std::pair<AttrId, AttrValue>> && cachedValue)
    : root(root), parent(parent), cachedValue(std::move(cachedValue))
{
    if (value)
        _value = allocRootValue(value);
}

AttrKey AttrCursor::getKey()
{
    if (!parent)
        return {0, root->state.sEpsilon};
    if (!parent->first->cachedValue) {
        parent->first->cachedValue = root->db->getAttr(
            parent->first->getKey(), root->state.symbols);
        assert(parent->first->cachedValue);
    }
    return {parent->first->cachedValue->first, parent->second};
}

Value & AttrCursor::getValue()
{
    if (!_value) {
        if (parent) {
            auto & vParent = parent->first->getValue();
            root->state.forceAttrs(vParent);
            auto attr = vParent.attrs->get(parent->second);
            if (!attr)
                throw Error("attribute '%s' is unexpectedly missing", getAttrPathStr());
            _value = allocRootValue(attr->value);
        } else
            _value = allocRootValue(root->getRootValue());
    }
    return **_value;
}

std::vector<Symbol> AttrCursor::getAttrPath() const
{
    if (parent) {
        auto attrPath = parent->first->getAttrPath();
        attrPath.push_back(parent->second);
        return attrPath;
    } else
        return {};
}

std::vector<Symbol> AttrCursor::getAttrPath(Symbol name) const
{
    auto attrPath = getAttrPath();
    attrPath.push_back(name);
    return attrPath;
}

std::string AttrCursor::getAttrPathStr() const
{
    return concatStringsSep(".", getAttrPath());
}

std::string AttrCursor::getAttrPathStr(Symbol name) const
{
    return concatStringsSep(".", getAttrPath(name));
}

Value & AttrCursor::forceValue()
{
    debug("evaluating uncached attribute %s", getAttrPathStr());

    auto & v = getValue();

    try {
        root->state.forceValue(v);
    } catch (EvalError &) {
        debug("setting '%s' to failed", getAttrPathStr());
        if (root->db)
            cachedValue = {root->db->setFailed(getKey()), failed_t()};
        throw;
    }

    if (root->db && (!cachedValue || std::get_if<placeholder_t>(&cachedValue->second))) {
        if (v.type == tString)
            cachedValue = {root->db->setString(getKey(), v.string.s, v.string.context), v.string.s};
        else if (v.type == tPath)
            cachedValue = {root->db->setString(getKey(), v.path), v.path};
        else if (v.type == tBool)
            cachedValue = {root->db->setBool(getKey(), v.boolean), v.boolean};
        else if (v.type == tAttrs)
            ; // FIXME: do something?
        else
            cachedValue = {root->db->setMisc(getKey()), misc_t()};
    }

    return v;
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(Symbol name)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey(), root->state.symbols);

        if (cachedValue) {
            if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                for (auto & attr : *attrs)
                    if (attr == name)
                        return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), name));
                return nullptr;
            } else if (std::get_if<placeholder_t>(&cachedValue->second)) {
                auto attr = root->db->getAttr({cachedValue->first, name}, root->state.symbols);
                if (attr) {
                    if (std::get_if<missing_t>(&attr->second))
                        return nullptr;
                    else if (std::get_if<failed_t>(&attr->second))
                        throw EvalError("cached failure of attribute '%s'", getAttrPathStr(name));
                    else
                        return std::make_shared<AttrCursor>(root,
                            std::make_pair(shared_from_this(), name), nullptr, std::move(attr));
                }
                // Incomplete attrset, so need to fall thru and
                // evaluate to see whether 'name' exists
            } else
                return nullptr;
                //throw TypeError("'%s' is not an attribute set", getAttrPathStr());
        }
    }

    auto & v = forceValue();

    if (v.type != tAttrs)
        return nullptr;
        //throw TypeError("'%s' is not an attribute set", getAttrPathStr());

    auto attr = v.attrs->get(name);

    if (!attr) {
        if (root->db) {
            if (!cachedValue)
                cachedValue = {root->db->setPlaceholder(getKey()), placeholder_t()};
            root->db->setMissing({cachedValue->first, name});
        }
        return nullptr;
    }

    std::optional<std::pair<AttrId, AttrValue>> cachedValue2;
    if (root->db) {
        if (!cachedValue)
            cachedValue = {root->db->setPlaceholder(getKey()), placeholder_t()};
        cachedValue2 = {root->db->setPlaceholder({cachedValue->first, name}), placeholder_t()};
    }

    return std::make_shared<AttrCursor>(
        root, std::make_pair(shared_from_this(), name), attr->value, std::move(cachedValue2));
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(std::string_view name)
{
    return maybeGetAttr(root->state.symbols.create(name));
}

std::shared_ptr<AttrCursor> AttrCursor::getAttr(Symbol name)
{
    auto p = maybeGetAttr(name);
    if (!p)
        throw Error("attribute '%s' does not exist", getAttrPathStr(name));
    return p;
}

std::shared_ptr<AttrCursor> AttrCursor::getAttr(std::string_view name)
{
    return getAttr(root->state.symbols.create(name));
}

std::shared_ptr<AttrCursor> AttrCursor::findAlongAttrPath(const std::vector<Symbol> & attrPath)
{
    auto res = shared_from_this();
    for (auto & attr : attrPath) {
        res = res->maybeGetAttr(attr);
        if (!res) return {};
    }
    return res;
}

std::string AttrCursor::getString()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey(), root->state.symbols);
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                debug("using cached string attribute '%s'", getAttrPathStr());
                return s->first;
            } else
                throw TypeError("'%s' is not a string", getAttrPathStr());
        }
    }

    auto & v = forceValue();

    if (v.type != tString && v.type != tPath)
        throw TypeError("'%s' is not a string but %s", getAttrPathStr(), showType(v.type));

    return v.type == tString ? v.string.s : v.path;
}

string_t AttrCursor::getStringWithContext()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey(), root->state.symbols);
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                debug("using cached string attribute '%s'", getAttrPathStr());
                return *s;
            } else
                throw TypeError("'%s' is not a string", getAttrPathStr());
        }
    }

    auto & v = forceValue();

    if (v.type == tString)
        return {v.string.s, v.getContext()};
    else if (v.type == tPath)
        return {v.path, {}};
    else
        throw TypeError("'%s' is not a string but %s", getAttrPathStr(), showType(v.type));
}

bool AttrCursor::getBool()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey(), root->state.symbols);
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto b = std::get_if<bool>(&cachedValue->second)) {
                debug("using cached Boolean attribute '%s'", getAttrPathStr());
                return *b;
            } else
                throw TypeError("'%s' is not a Boolean", getAttrPathStr());
        }
    }

    auto & v = forceValue();

    if (v.type != tBool)
        throw TypeError("'%s' is not a Boolean", getAttrPathStr());

    return v.boolean;
}

std::vector<Symbol> AttrCursor::getAttrs()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey(), root->state.symbols);
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                debug("using cached attrset attribute '%s'", getAttrPathStr());
                return *attrs;
            } else
                throw TypeError("'%s' is not an attribute set", getAttrPathStr());
        }
    }

    auto & v = forceValue();

    if (v.type != tAttrs)
        throw TypeError("'%s' is not an attribute set", getAttrPathStr());

    std::vector<Symbol> attrs;
    for (auto & attr : *getValue().attrs)
        attrs.push_back(attr.name);
    std::sort(attrs.begin(), attrs.end(), [](const Symbol & a, const Symbol & b) {
        return (const string &) a < (const string &) b;
    });

    if (root->db)
        cachedValue = {root->db->setAttrs(getKey(), attrs), attrs};

    return attrs;
}

bool AttrCursor::isDerivation()
{
    auto aType = maybeGetAttr("type");
    return aType && aType->getString() == "derivation";
}

StorePath AttrCursor::forceDerivation()
{
    auto aDrvPath = getAttr(root->state.sDrvPath);
    auto drvPath = root->state.store->parseStorePath(aDrvPath->getString());
    if (!root->state.store->isValidPath(drvPath) && !settings.readOnlyMode) {
        /* The eval cache contains 'drvPath', but the actual path has
           been garbage-collected. So force it to be regenerated. */
        aDrvPath->forceValue();
        if (!root->state.store->isValidPath(drvPath))
            throw Error("don't know how to recreate store derivation '%s'!",
                root->state.store->printStorePath(drvPath));
    }
    return drvPath;
}

}
