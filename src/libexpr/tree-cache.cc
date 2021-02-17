#include "tree-cache.hh"
#include "sqlite.hh"
#include "store-api.hh"
#include "context.hh"

namespace nix::tree_cache {

static const char * schema = R"sql(
create table if not exists Attributes (
    id          integer primary key autoincrement not null,
    parent      integer not null,
    name        text,
    type        integer not null,
    value       text,
    context     text,
    unique      (parent, name)
);

create index if not exists IndexByParent on Attributes(parent, name);
)sql";

struct AttrDb
{
    std::atomic_bool failed{false};

    struct State
    {
        SQLite db;
        SQLiteStmt insertAttribute;
        SQLiteStmt updateAttribute;
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

        Path cacheDir = getCacheDir() + "/nix/eval-cache-v3";
        createDirs(cacheDir);

        Path dbPath = cacheDir + "/" + fingerprint.to_string(Base16, false) + ".sqlite";

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->insertAttribute.create(state->db,
            "insert into Attributes(parent, name, type, value) values (?, ?, ?, ?)");

        state->updateAttribute.create(state->db,
            "update Attributes set type = ?, value = ?, context = ? where id = ?");

        state->insertAttributeWithContext.create(state->db,
            "insert into Attributes(parent, name, type, value, context) values (?, ?, ?, ?, ?)");

        state->queryAttribute.create(state->db,
            "select id, type, value, context from Attributes where parent = ? and name = ?");

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

    /**
     * Store a leaf of the tree in the db
     */
    AttrId addEntry(
        const AttrKey & key,
        const AttrValue & value)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());
            auto rawValue = RawValue::fromVariant(value);

            state->insertAttributeWithContext.use()
                (key.first)
                (key.second)
                (rawValue.type)
                (rawValue.value.value_or(""), rawValue.value.has_value())
                (rawValue.serializeContext())
                .exec();
            AttrId rowId = state->db.getLastInsertedRowId();
            assert(rowId);
            return rowId;
        });
    }

    std::optional<AttrId> getId(const AttrKey& key)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(key.second));
        if (!queryAttribute.next()) return std::nullopt;

        return (AttrType) queryAttribute.getInt(0);
    }

    AttrId setOrUpdate(const AttrKey& key, const AttrValue& value)
    {
        debug("cache: miss for the attribute %s", key.second);
        if (auto existingId = getId(key)) {
            setValue(*existingId, value);
            return *existingId;
        }
        return addEntry(key, value);
    }

    void setValue(const AttrId & id, const AttrValue & value)
    {
        auto state(_state->lock());
            auto rawValue = RawValue::fromVariant(value);

            state->updateAttribute.use()
                (rawValue.type)
                (rawValue.value.value_or(""), rawValue.value.has_value())
                (rawValue.serializeContext())
                (id)
                .exec();
    }

    std::optional<std::pair<AttrId, AttrValue>> getValue(AttrKey key)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(key.second));
        if (!queryAttribute.next()) return {};

        auto rowId = (AttrType) queryAttribute.getInt(0);
        auto type = (AttrType) queryAttribute.getInt(1);

        switch (type) {
            case AttrType::Attrs: {
                return {{rowId, attributeSet_t()}};
            }
            case AttrType::String: {
                std::vector<std::pair<Path, std::string>> context;
                if (!queryAttribute.isNull(3))
                    for (auto & s : tokenizeString<std::vector<std::string>>(queryAttribute.getStr(3), ";"))
                        context.push_back(decodeContext(s));
                return {{rowId, string_t{queryAttribute.getStr(2), context}}};
            }
            case AttrType::Bool:
                return {{rowId, wrapped_basetype<bool>{queryAttribute.getInt(2) != 0}}};
            case AttrType::Int:
                return {{rowId, wrapped_basetype<int64_t>{queryAttribute.getInt(2)}}};
            case AttrType::Double:
                return {{rowId, wrapped_basetype<double>{(double)queryAttribute.getInt(2)}}};
            case AttrType::Unknown:
                return {{rowId, unknown_t{}}};
            case AttrType::Thunk:
                return {{rowId, thunk_t{}}};
            case AttrType::Missing:
                return {{rowId, missing_t{key.second}}};
            case AttrType::Failed:
                return {{rowId, failed_t{queryAttribute.getStr(2)}}};
            default:
                throw Error("unexpected type in evaluation cache");
        }
    }

    std::vector<std::string> getChildren(AttrId parentId)
    {
        std::vector<std::string> res;
        auto state(_state->lock());

        auto queryAttributes(state->queryAttributes.use()(parentId));

        while (queryAttributes.next())
            res.push_back(queryAttributes.getStr(0));

        return res;
    }
};

Cache::Cache(const Hash & useCache,
        SymbolTable & symbols)
    : db(std::make_shared<AttrDb>(useCache))
    , symbols(symbols)
    , rootSymbol(symbols.create(""))
{
}

std::shared_ptr<Cache> Cache::tryCreate(const Hash & useCache, SymbolTable & symbols)
{
    try {
        return std::make_shared<Cache>(useCache, symbols);
    } catch (SQLiteError &) {
        ignoreException();
        return nullptr;
    }
}

void Cache::commit()
{
    if (db) {
        debug("Saving the cache");
        auto state(db->_state->lock());
        if (state->txn->active) {
            state->txn->commit();
            state->txn.reset();
            state->txn = std::make_unique<SQLiteTxn>(state->db);
        }
    }
}

Cursor::Ref Cache::getRoot()
{
    return new Cursor(ref(shared_from_this()), std::nullopt, thunk_t{});
}

Cursor::Cursor(
    ref<Cache> root,
    const Parent & parent,
    const AttrValue& value
    )
    : root(root)
    , parentId(parent ? std::optional{parent->first.cachedValue.first} : std::nullopt)
    , label(parent ? parent->second : root->rootSymbol)
    , cachedValue({root->db->setOrUpdate(getKey(), value), value})
{
}

Cursor::Cursor(
    ref<Cache> root,
    const Parent & parent,
    const AttrId & id,
    const AttrValue & value
    )
    : root(root)
    , parentId(parent ? std::optional{parent->first.cachedValue.first} : std::nullopt)
    , label(parent ? parent->second : root->rootSymbol)
    , cachedValue({id, value})
{
}


AttrKey Cursor::getKey()
{
    if (!parentId)
        return {0, root->rootSymbol};
    return {*parentId, label};
}

AttrValue Cursor::getCachedValue()
{
    return cachedValue.second;
}

void Cursor::setValue(const AttrValue & v)
{
    root->db->setValue(cachedValue.first, v);
    cachedValue.second = v;
}

Cursor::Ref Cursor::addChild(const Symbol & attrPath, const AttrValue & v)
{
    Parent parent = {{*this, attrPath}};
    auto childCursor = new Cursor(
        root,
        parent,
        v
    );
    return childCursor;
}

std::vector<std::string> Cursor::getChildren()
{
    return root->db->getChildren(cachedValue.first);
}

std::optional<std::vector<std::string>> Cursor::getChildrenAtPath(const std::vector<Symbol> & attrPath)
{
    auto cursorAtPath = findAlongAttrPath(attrPath);
    if (cursorAtPath)
        return cursorAtPath->getChildren();
    return std::nullopt;
}

Cursor::Ref Cursor::maybeGetAttr(const Symbol & name)
{
    auto rawAttr = root->db->getValue({cachedValue.first, name});
    if (rawAttr) {
        Parent parent = {{*this, name}};
        debug("cache: hit for the attribute %s", cachedValue.first);
        return new Cursor (
            root, parent, rawAttr->first,
            rawAttr->second);
    }
    if (std::holds_alternative<attributeSet_t>(cachedValue.second)) {
        // If the parent is an attribute set but we're not present in the db,
        // then we're not a member of this attribute set. So mark as missing
        return addChild(name, missing_t{name});
    }
    return nullptr;
}

Cursor::Ref Cursor::findAlongAttrPath(const std::vector<Symbol> & attrPath)
{
    auto currentCursor = this;
    for (auto & currentAccessor : attrPath) {
        currentCursor = currentCursor->maybeGetAttr(currentAccessor);
        if (!currentCursor)
            break;
        if (std::holds_alternative<missing_t>(currentCursor->cachedValue.second))
            break;
        if (std::holds_alternative<failed_t>(currentCursor->cachedValue.second))
            break;
    }
    return currentCursor;
}

const RawValue RawValue::fromVariant(const AttrValue & value)
{
    RawValue res;
    std::visit(overloaded{
      [&](attributeSet_t x) { res.type = AttrType::Attrs; },
      [&](string_t x) {
        res.type = AttrType::String;
        res.value = x.first;
        res.context = x.second;
      },
      [&](wrapped_basetype<bool> x) {
        res.type = AttrType::Bool;
        res.value = x.value ? "1" : "0";
      },
      [&](wrapped_basetype<int64_t> x) {
        res.type = AttrType::Int;
        res.value = std::to_string(x.value);
      },
      [&](wrapped_basetype<double> x) {
        res.type = AttrType::Double;
        res.value = std::to_string(x.value);
      },
      [&](unknown_t x) { res.type = AttrType::Unknown; },
      [&](missing_t x) { res.type = AttrType::Missing; },
      [&](thunk_t x) { res.type = AttrType::Thunk; },
      [&](failed_t x) {
          res.type = AttrType::Failed;
          res.value = x.error;
      }
    }, value);
    return res;
}

std::string RawValue::serializeContext() const
{
    std::string res;
    for (auto & elt : context) {
        res.append(encodeContext(elt.second, elt.first));
        res.push_back(' ');
    }
    if (!res.empty())
        res.pop_back(); // Remove the trailing space
    return res;
}

}
