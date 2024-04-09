#include "users.hh"
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

    const StoreDirConfig & cfg;

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

    SymbolTable & symbols;

    AttrDb(
        const StoreDirConfig & cfg,
        const Hash & fingerprint,
        SymbolTable & symbols)
        : cfg(cfg)
        , _state(std::make_unique<Sync<State>>())
        , symbols(symbols)
    {
        auto state(_state->lock());

        Path cacheDir = getCacheDir() + "/nix/eval-cache-v5";
        createDirs(cacheDir);

        Path dbPath = cacheDir + "/" + fingerprint.to_string(HashFormat::Base16, false) + ".sqlite";

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
                (symbols[key.second])
                (AttrType::FullAttrs)
                (0, false).exec();

            AttrId rowId = state->db.getLastInsertedRowId();
            assert(rowId);

            for (auto & attr : attrs)
                state->insertAttribute.use()
                    (rowId)
                    (symbols[attr])
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
                    (symbols[key.second])
                    (AttrType::String)
                    (s)
                    (ctx).exec();
            } else {
                state->insertAttribute.use()
                    (key.first)
                    (symbols[key.second])
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
                (symbols[key.second])
                (AttrType::Bool)
                (b ? 1 : 0).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setInt(
        AttrKey key,
        int n)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (symbols[key.second])
                (AttrType::Int)
                (n).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setListOfStrings(
        AttrKey key,
        const std::vector<std::string> & l)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (symbols[key.second])
                (AttrType::ListOfStrings)
                (concatStringsSep("\t", l)).exec();

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
                (symbols[key.second])
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
                (symbols[key.second])
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
                (symbols[key.second])
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
                (symbols[key.second])
                (AttrType::Failed)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    std::optional<std::pair<AttrId, AttrValue>> getAttr(AttrKey key)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(symbols[key.second]));
        if (!queryAttribute.next()) return {};

        auto rowId = (AttrId) queryAttribute.getInt(0);
        auto type = (AttrType) queryAttribute.getInt(1);

        switch (type) {
            case AttrType::Placeholder:
                return {{rowId, placeholder_t()}};
            case AttrType::FullAttrs: {
                // FIXME: expensive, should separate this out.
                std::vector<Symbol> attrs;
                auto queryAttributes(state->queryAttributes.use()(rowId));
                while (queryAttributes.next())
                    attrs.emplace_back(symbols.create(queryAttributes.getStr(0)));
                return {{rowId, attrs}};
            }
            case AttrType::String: {
                NixStringContext context;
                if (!queryAttribute.isNull(3))
                    for (auto & s : tokenizeString<std::vector<std::string>>(queryAttribute.getStr(3), ";"))
                        context.insert(NixStringContextElem::parse(s));
                return {{rowId, string_t{queryAttribute.getStr(2), context}}};
            }
            case AttrType::Bool:
                return {{rowId, queryAttribute.getInt(2) != 0}};
            case AttrType::Int:
                return {{rowId, int_t{queryAttribute.getInt(2)}}};
            case AttrType::ListOfStrings:
                return {{rowId, tokenizeString<std::vector<std::string>>(queryAttribute.getStr(2), "\t")}};
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

static std::shared_ptr<AttrDb> makeAttrDb(
    const StoreDirConfig & cfg,
    const Hash & fingerprint,
    SymbolTable & symbols)
{
    try {
        return std::make_shared<AttrDb>(cfg, fingerprint, symbols);
    } catch (SQLiteError &) {
        ignoreException();
        return nullptr;
    }
}

EvalCache::EvalCache(
    std::optional<std::reference_wrapper<const Hash>> useCache,
    EvalState & state,
    RootLoader rootLoader)
    : db(useCache ? makeAttrDb(*state.store, *useCache, state.symbols) : nullptr)
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

ref<AttrCursor> EvalCache::getRoot()
{
    return make_ref<AttrCursor>(ref(shared_from_this()), std::nullopt);
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
        parent->first->cachedValue = root->db->getAttr(parent->first->getKey());
        assert(parent->first->cachedValue);
    }
    return {parent->first->cachedValue->first, parent->second};
}

Value & AttrCursor::getValue()
{
    if (!_value) {
        if (parent) {
            auto & vParent = parent->first->getValue();
            root->state.forceAttrs(vParent, noPos, "while searching for an attribute");
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
    return concatStringsSep(".", root->state.symbols.resolve(getAttrPath()));
}

std::string AttrCursor::getAttrPathStr(Symbol name) const
{
    return concatStringsSep(".", root->state.symbols.resolve(getAttrPath(name)));
}

Value & AttrCursor::forceValue()
{
    debug("evaluating uncached attribute '%s'", getAttrPathStr());

    auto & v = getValue();

    try {
        root->state.forceValue(v, noPos);
    } catch (EvalError &) {
        debug("setting '%s' to failed", getAttrPathStr());
        if (root->db)
            cachedValue = {root->db->setFailed(getKey()), failed_t()};
        throw;
    }

    if (root->db && (!cachedValue || std::get_if<placeholder_t>(&cachedValue->second))) {
        if (v.type() == nString)
            cachedValue = {root->db->setString(getKey(), v.c_str(), v.context()),
                           string_t{v.c_str(), {}}};
        else if (v.type() == nPath) {
            auto path = v.path().path;
            cachedValue = {root->db->setString(getKey(), path.abs()), string_t{path.abs(), {}}};
        }
        else if (v.type() == nBool)
            cachedValue = {root->db->setBool(getKey(), v.boolean), v.boolean};
        else if (v.type() == nInt)
            cachedValue = {root->db->setInt(getKey(), v.integer), int_t{v.integer}};
        else if (v.type() == nAttrs)
            ; // FIXME: do something?
        else
            cachedValue = {root->db->setMisc(getKey()), misc_t()};
    }

    return v;
}

Suggestions AttrCursor::getSuggestionsForAttr(Symbol name)
{
    auto attrNames = getAttrs();
    std::set<std::string> strAttrNames;
    for (auto & name : attrNames)
        strAttrNames.insert(root->state.symbols[name]);

    return Suggestions::bestMatches(strAttrNames, root->state.symbols[name]);
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(Symbol name, bool forceErrors)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());

        if (cachedValue) {
            if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                for (auto & attr : *attrs)
                    if (attr == name)
                        return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), attr));
                return nullptr;
            } else if (std::get_if<placeholder_t>(&cachedValue->second)) {
                auto attr = root->db->getAttr({cachedValue->first, name});
                if (attr) {
                    if (std::get_if<missing_t>(&attr->second))
                        return nullptr;
                    else if (std::get_if<failed_t>(&attr->second)) {
                        if (forceErrors)
                            debug("reevaluating failed cached attribute '%s'", getAttrPathStr(name));
                        else
                            throw CachedEvalError(root->state, "cached failure of attribute '%s'", getAttrPathStr(name));
                    } else
                        return std::make_shared<AttrCursor>(root,
                            std::make_pair(shared_from_this(), name), nullptr, std::move(attr));
                }
                // Incomplete attrset, so need to fall thru and
                // evaluate to see whether 'name' exists
            } else
                return nullptr;
                //error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nAttrs)
        return nullptr;
        //error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();

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

    return make_ref<AttrCursor>(
        root, std::make_pair(shared_from_this(), name), attr->value, std::move(cachedValue2));
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(std::string_view name)
{
    return maybeGetAttr(root->state.symbols.create(name));
}

ref<AttrCursor> AttrCursor::getAttr(Symbol name, bool forceErrors)
{
    auto p = maybeGetAttr(name, forceErrors);
    if (!p)
        throw Error("attribute '%s' does not exist", getAttrPathStr(name));
    return ref(p);
}

ref<AttrCursor> AttrCursor::getAttr(std::string_view name)
{
    return getAttr(root->state.symbols.create(name));
}

OrSuggestions<ref<AttrCursor>> AttrCursor::findAlongAttrPath(const std::vector<Symbol> & attrPath, bool force)
{
    auto res = shared_from_this();
    for (auto & attr : attrPath) {
        auto child = res->maybeGetAttr(attr, force);
        if (!child) {
            auto suggestions = res->getSuggestionsForAttr(attr);
            return OrSuggestions<ref<AttrCursor>>::failed(suggestions);
        }
        res = child;
    }
    return ref(res);
}

std::string AttrCursor::getString()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                debug("using cached string attribute '%s'", getAttrPathStr());
                return s->first;
            } else
                root->state.error<TypeError>("'%s' is not a string", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nString && v.type() != nPath)
        root->state.error<TypeError>("'%s' is not a string but %s", getAttrPathStr(), showType(v)).debugThrow();

    return v.type() == nString ? v.c_str() : v.path().to_string();
}

string_t AttrCursor::getStringWithContext()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                bool valid = true;
                for (auto & c : s->second) {
                    const StorePath & path = std::visit(overloaded {
                        [&](const NixStringContextElem::DrvDeep & d) -> const StorePath & {
                            return d.drvPath;
                        },
                        [&](const NixStringContextElem::Built & b) -> const StorePath & {
                            return b.drvPath->getBaseStorePath();
                        },
                        [&](const NixStringContextElem::Opaque & o) -> const StorePath & {
                            return o.path;
                        },
                    }, c.raw);
                    if (!root->state.store->isValidPath(path)) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    debug("using cached string attribute '%s'", getAttrPathStr());
                    return *s;
                }
            } else
                root->state.error<TypeError>("'%s' is not a string", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() == nString) {
        NixStringContext context;
        copyContext(v, context);
        return {v.c_str(), std::move(context)};
    }
    else if (v.type() == nPath)
        return {v.path().to_string(), {}};
    else
        root->state.error<TypeError>("'%s' is not a string but %s", getAttrPathStr(), showType(v)).debugThrow();
}

bool AttrCursor::getBool()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto b = std::get_if<bool>(&cachedValue->second)) {
                debug("using cached Boolean attribute '%s'", getAttrPathStr());
                return *b;
            } else
                root->state.error<TypeError>("'%s' is not a Boolean", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nBool)
        root->state.error<TypeError>("'%s' is not a Boolean", getAttrPathStr()).debugThrow();

    return v.boolean;
}

NixInt AttrCursor::getInt()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto i = std::get_if<int_t>(&cachedValue->second)) {
                debug("using cached integer attribute '%s'", getAttrPathStr());
                return i->x;
            } else
                root->state.error<TypeError>("'%s' is not an integer", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nInt)
        root->state.error<TypeError>("'%s' is not an integer", getAttrPathStr()).debugThrow();

    return v.integer;
}

std::vector<std::string> AttrCursor::getListOfStrings()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto l = std::get_if<std::vector<std::string>>(&cachedValue->second)) {
                debug("using cached list of strings attribute '%s'", getAttrPathStr());
                return *l;
            } else
                root->state.error<TypeError>("'%s' is not a list of strings", getAttrPathStr()).debugThrow();
        }
    }

    debug("evaluating uncached attribute '%s'", getAttrPathStr());

    auto & v = getValue();
    root->state.forceValue(v, noPos);

    if (v.type() != nList)
        root->state.error<TypeError>("'%s' is not a list", getAttrPathStr()).debugThrow();

    std::vector<std::string> res;

    for (auto & elem : v.listItems())
        res.push_back(std::string(root->state.forceStringNoCtx(*elem, noPos, "while evaluating an attribute for caching")));

    if (root->db)
        cachedValue = {root->db->setListOfStrings(getKey(), res), res};

    return res;
}

std::vector<Symbol> AttrCursor::getAttrs()
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                debug("using cached attrset attribute '%s'", getAttrPathStr());
                return *attrs;
            } else
                root->state.error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nAttrs)
        root->state.error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();

    std::vector<Symbol> attrs;
    for (auto & attr : *getValue().attrs)
        attrs.push_back(attr.name);
    std::sort(attrs.begin(), attrs.end(), [&](Symbol a, Symbol b) {
        std::string_view sa = root->state.symbols[a], sb = root->state.symbols[b];
        return sa < sb;
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
    auto aDrvPath = getAttr(root->state.sDrvPath, true);
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
