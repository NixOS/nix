#include "nix/util/users.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/store/sqlite.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"
// Need specialization involving `SymbolStr` just in this one module.
#include "nix/util/strings-inline.hh"

#include <expected>

namespace nix::eval_cache {

/**
 * Error type for eval-cache database operations.
 * Used with std::expected for explicit error handling.
 */
enum class CacheError {
    /**
     * Database previously failed, all operations are disabled.
     */
    DatabaseFailed,
    /**
     * A SQLite error occurred during the operation.
     */
    DatabaseError,
};

/**
 * Type alias for the result of getAttr operations.
 * Returns:
 * - std::nullopt inside expected: attribute not found (success case)
 * - Has value inside expected: attribute found with its data
 * - unexpected(CacheError): database error occurred
 */
using AttrResult = std::expected<std::optional<std::pair<AttrId, AttrValue>>, CacheError>;

CachedEvalError::CachedEvalError(ref<AttrCursor> cursor, Symbol attr)
    : EvalError(cursor->root->state, "cached failure of attribute '%s'", cursor->getAttrPathStr(attr))
    , cursor(cursor)
    , attr(attr)
{
}

void CachedEvalError::force()
{
    auto & v = cursor->forceValue();

    if (v.type() == nAttrs) {
        auto a = v.attrs()->get(this->attr);

        state.forceValue(*a->value, a->pos);
    }

    // Shouldn't happen.
    throw EvalError(
        state, "evaluation of cached failed attribute '%s' unexpectedly succeeded", cursor->getAttrPathStr(attr));
}

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
    static constexpr size_t maxSQLiteRetries = 100;

    const StoreDirConfig & cfg;

    struct State
    {
        SQLite db;
        SQLiteStmt insertAttribute;
        SQLiteStmt insertAttributeWithContext;
        SQLiteStmt queryAttribute;
        SQLiteStmt queryAttributes;
    };

    std::unique_ptr<Sync<State>> _state;

    SymbolTable & symbols;

    AttrDb(const StoreDirConfig & cfg, const Hash & fingerprint, SymbolTable & symbols)
        : cfg(cfg)
        , _state(std::make_unique<Sync<State>>())
        , symbols(symbols)
    {
        auto state(_state->lock());

        auto cacheDir = std::filesystem::path(getCacheDir()) / "eval-cache-v6";
        createDirs(cacheDir);

        auto dbPath = cacheDir / (fingerprint.to_string(HashFormat::Base16, false) + ".sqlite");

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->insertAttribute.create(
            state->db, "insert or replace into Attributes(parent, name, type, value) values (?, ?, ?, ?)");

        state->insertAttributeWithContext.create(
            state->db, "insert or replace into Attributes(parent, name, type, value, context) values (?, ?, ?, ?, ?)");

        state->queryAttribute.create(
            state->db, "select rowid, type, value, context from Attributes where parent = ? and name = ?");

        state->queryAttributes.create(state->db, "select name from Attributes where parent = ?");
    }

    ~AttrDb()
    {
        // No transaction to commit - each operation is self-contained
    }

    /**
     * Execute a write operation with retry logic and graceful degradation.
     *
     * Uses Immediate transaction mode to acquire the write lock upfront,
     * which ensures busy_timeout is respected and enables effective retry
     * logic when the database is locked by another process.
     *
     * On SQLite errors (other than SQLITE_BUSY which is retried), marks
     * the database as failed and returns an error. Once failed, all
     * subsequent operations return CacheError::DatabaseFailed.
     *
     * @param fun Lambda that performs the actual database write(s) and
     *            returns an AttrId. Receives a locked State reference.
     * @return The AttrId returned by fun on success, or CacheError on failure.
     */
    template<typename F>
    [[nodiscard]] std::expected<AttrId, CacheError> doSQLiteWrite(F && fun)
    {
        if (failed)
            return std::unexpected(CacheError::DatabaseFailed);
        try {
            return retrySQLite<AttrId>(
                [&]() {
                    auto state(_state->lock());
                    SQLiteTxn txn(state->db, SQLiteTxnMode::Immediate);
                    AttrId rowId = fun(state);
                    txn.commit();
                    return rowId;
                },
                maxSQLiteRetries);
        } catch (SQLiteError &) {
            ignoreExceptionExceptInterrupt();
            failed = true;
            return std::unexpected(CacheError::DatabaseError);
        }
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setAttrs(AttrKey key, const std::vector<Symbol> & attrs)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::FullAttrs) (0, false).exec();

            AttrId rowId = state->db.getLastInsertedRowId();
            assert(rowId);

            for (auto & attr : attrs)
                state->insertAttribute.use()(rowId)(symbols[attr])(AttrType::Placeholder) (0, false).exec();

            return rowId;
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError>
    setString(AttrKey key, std::string_view s, const Value::StringWithContext::Context * context = nullptr)
    {
        return doSQLiteWrite([&](auto & state) {
            if (context) {
                std::string ctx;
                bool first = true;
                for (auto * elem : *context) {
                    if (!first)
                        ctx.push_back(' ');
                    ctx.append(elem->view());
                    first = false;
                }
                state->insertAttributeWithContext.use()(key.first)(symbols[key.second])(AttrType::String) (s) (ctx)
                    .exec();
            } else {
                state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::String) (s).exec();
            }

            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setBool(AttrKey key, bool b)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Bool) (b ? 1 : 0).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setInt(AttrKey key, int n)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Int) (n).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setListOfStrings(AttrKey key, const std::vector<std::string> & l)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute
                .use()(key.first)(symbols[key.second])(
                    AttrType::ListOfStrings) (dropEmptyInitThenConcatStringsSep("\t", l))
                .exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setPlaceholder(AttrKey key)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Placeholder) (0, false).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setMissing(AttrKey key)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Missing) (0, false).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setMisc(AttrKey key)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Misc) (0, false).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    [[nodiscard]] std::expected<AttrId, CacheError> setFailed(AttrKey key)
    {
        return doSQLiteWrite([&](auto & state) {
            state->insertAttribute.use()(key.first)(symbols[key.second])(AttrType::Failed) (0, false).exec();
            return state->db.getLastInsertedRowId();
        });
    }

    /**
     * Retrieve an attribute from the cache.
     *
     * Uses a transaction to ensure consistent reads when the FullAttrs
     * case requires multiple queries.
     *
     * @return AttrResult containing:
     *         - std::nullopt if the attribute is not in the cache
     *         - The attribute data if found
     *         - CacheError on database failure
     */
    [[nodiscard]] AttrResult getAttr(AttrKey key)
    {
        if (failed)
            return std::unexpected(CacheError::DatabaseFailed);
        try {
            return retrySQLite<std::optional<std::pair<AttrId, AttrValue>>>(
                [&]() {
                    auto state(_state->lock());
                    // Use a transaction to ensure consistent reads, especially for
                    // FullAttrs which requires two queries (queryAttribute + queryAttributes)
                    SQLiteTxn txn(state->db);

                    auto queryAttribute(state->queryAttribute.use()(key.first)(symbols[key.second]));
                    if (!queryAttribute.next())
                        return std::optional<std::pair<AttrId, AttrValue>>{};

                    auto rowId = (AttrId) queryAttribute.getInt(0);
                    auto type = (AttrType) queryAttribute.getInt(1);

                    std::optional<std::pair<AttrId, AttrValue>> result;

                    switch (type) {
                    case AttrType::Placeholder:
                        result = {{rowId, placeholder_t()}};
                        break;
                    case AttrType::FullAttrs: {
                        // FIXME: expensive, should separate this out.
                        std::vector<Symbol> attrs;
                        auto queryAttributes(state->queryAttributes.use()(rowId));
                        while (queryAttributes.next())
                            attrs.emplace_back(symbols.create(queryAttributes.getStr(0)));
                        result = {{rowId, std::move(attrs)}};
                        break;
                    }
                    case AttrType::String: {
                        NixStringContext context;
                        if (!queryAttribute.isNull(3))
                            for (auto & s : tokenizeString<std::vector<std::string>>(queryAttribute.getStr(3), " "))
                                context.insert(NixStringContextElem::parse(s));
                        result = {{rowId, string_t{queryAttribute.getStr(2), std::move(context)}}};
                        break;
                    }
                    case AttrType::Bool:
                        result = {{rowId, queryAttribute.getInt(2) != 0}};
                        break;
                    case AttrType::Int:
                        result = {{rowId, int_t{NixInt{queryAttribute.getInt(2)}}}};
                        break;
                    case AttrType::ListOfStrings:
                        result = {{rowId, tokenizeString<std::vector<std::string>>(queryAttribute.getStr(2), "\t")}};
                        break;
                    case AttrType::Missing:
                        result = {{rowId, missing_t()}};
                        break;
                    case AttrType::Misc:
                        result = {{rowId, misc_t()}};
                        break;
                    case AttrType::Failed:
                        result = {{rowId, failed_t()}};
                        break;
                    default:
                        throw Error("unexpected type in evaluation cache");
                    }

                    // Transaction rolls back when txn goes out of scope (no explicit commit needed).
                    // For read-only operations, rollback has no effect since nothing was modified.
                    return result;
                },
                maxSQLiteRetries);
        } catch (SQLiteError &) {
            ignoreExceptionExceptInterrupt();
            failed = true;
            return std::unexpected(CacheError::DatabaseError);
        }
    }
};

static std::shared_ptr<AttrDb> makeAttrDb(const StoreDirConfig & cfg, const Hash & fingerprint, SymbolTable & symbols)
{
    try {
        return std::make_shared<AttrDb>(cfg, fingerprint, symbols);
    } catch (SQLiteError &) {
        ignoreExceptionExceptInterrupt();
        return nullptr;
    }
}

EvalCache::EvalCache(
    std::optional<std::reference_wrapper<const Hash>> useCache, EvalState & state, RootLoader rootLoader)
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
    ref<EvalCache> root, Parent parent, Value * value, std::optional<std::pair<AttrId, AttrValue>> && cachedValue)
    : root(root)
    , parent(parent)
    , cachedValue(std::move(cachedValue))
{
    if (value)
        _value = allocRootValue(value);
}

AttrKey AttrCursor::getKey()
{
    if (!parent)
        return {0, root->state.s.epsilon};
    if (!parent->first->cachedValue) {
        auto result = root->db->getAttr(parent->first->getKey());
        if (result) {
            if (*result) {
                parent->first->cachedValue = *result;
            } else {
                // Not found in cache - set placeholder with AttrId=0
                parent->first->cachedValue = {{0, placeholder_t()}};
            }
        } else {
            // Database error - set placeholder to avoid crash (graceful degradation)
            parent->first->cachedValue = {{0, placeholder_t()}};
        }
    }
    return {parent->first->cachedValue->first, parent->second};
}

Value & AttrCursor::getValue()
{
    if (!_value) {
        if (parent) {
            auto & vParent = parent->first->getValue();
            root->state.forceAttrs(vParent, noPos, "while searching for an attribute");
            auto attr = vParent.attrs()->get(parent->second);
            if (!attr)
                throw Error("attribute '%s' is unexpectedly missing", getAttrPathStr());
            _value = allocRootValue(attr->value);
        } else
            _value = allocRootValue(root->getRootValue());
    }
    return **_value;
}

void AttrCursor::fetchCachedValue()
{
    if (!cachedValue) {
        if (auto result = root->db->getAttr(getKey()))
            cachedValue = *result;
    }
    if (cachedValue && std::get_if<failed_t>(&cachedValue->second) && parent)
        throw CachedEvalError(parent->first, parent->second);
}

AttrPath AttrCursor::getAttrPath() const
{
    if (parent) {
        auto attrPath = parent->first->getAttrPath();
        attrPath.push_back(parent->second);
        return attrPath;
    } else
        return {};
}

AttrPath AttrCursor::getAttrPath(Symbol name) const
{
    auto attrPath = getAttrPath();
    attrPath.push_back(name);
    return attrPath;
}

std::string AttrCursor::getAttrPathStr() const
{
    return getAttrPath().to_string(root->state);
}

std::string AttrCursor::getAttrPathStr(Symbol name) const
{
    return getAttrPath(name).to_string(root->state);
}

Value & AttrCursor::forceValue()
{
    debug("evaluating uncached attribute '%s'", getAttrPathStr());

    auto & v = getValue();

    try {
        root->state.forceValue(v, noPos);
    } catch (EvalError &) {
        debug("setting '%s' to failed", getAttrPathStr());
        if (root->db) {
            if (auto id = root->db->setFailed(getKey()))
                cachedValue = {*id, failed_t()};
        }
        throw;
    }

    if (root->db && (!cachedValue || std::get_if<placeholder_t>(&cachedValue->second))) {
        if (v.type() == nString) {
            if (auto id = root->db->setString(getKey(), v.string_view(), v.context()))
                cachedValue = {*id, string_t{std::string(v.string_view()), {}}};
        } else if (v.type() == nPath) {
            auto path = v.path().path;
            if (auto id = root->db->setString(getKey(), path.abs()))
                cachedValue = {*id, string_t{path.abs(), {}}};
        } else if (v.type() == nBool) {
            if (auto id = root->db->setBool(getKey(), v.boolean()))
                cachedValue = {*id, v.boolean()};
        } else if (v.type() == nInt) {
            if (auto id = root->db->setInt(getKey(), v.integer().value))
                cachedValue = {*id, int_t{v.integer()}};
        } else if (v.type() == nAttrs) {
            ; // FIXME: do something?
        } else {
            if (auto id = root->db->setMisc(getKey()))
                cachedValue = {*id, misc_t()};
        }
    }

    return v;
}

Suggestions AttrCursor::getSuggestionsForAttr(Symbol name)
{
    auto attrNames = getAttrs();
    StringSet strAttrNames;
    for (auto & name : attrNames)
        strAttrNames.insert(std::string(root->state.symbols[name]));

    return Suggestions::bestMatches(strAttrNames, root->state.symbols[name]);
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(Symbol name)
{
    if (root->db) {
        fetchCachedValue();

        if (cachedValue) {
            if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                for (auto & attr : *attrs)
                    if (attr == name)
                        return std::make_shared<AttrCursor>(root, std::make_pair(ref(shared_from_this()), attr));
                return nullptr;
            } else if (std::get_if<placeholder_t>(&cachedValue->second)) {
                if (auto result = root->db->getAttr({cachedValue->first, name})) {
                    if (auto & attr = *result) {
                        if (std::get_if<missing_t>(&attr->second))
                            return nullptr;
                        else if (std::get_if<failed_t>(&attr->second))
                            throw CachedEvalError(ref(shared_from_this()), name);
                        else
                            return std::make_shared<AttrCursor>(
                                root, std::make_pair(ref(shared_from_this()), name), nullptr, std::move(attr));
                    }
                }
                // Incomplete attrset, or db error - fall thru and
                // evaluate to see whether 'name' exists
            } else
                return nullptr;
            // error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue();

    if (v.type() != nAttrs)
        return nullptr;
    // error<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();

    auto attr = v.attrs()->get(name);

    if (!attr) {
        if (root->db) {
            if (!cachedValue) {
                if (auto id = root->db->setPlaceholder(getKey()))
                    cachedValue = {*id, placeholder_t()};
            }
            if (cachedValue)
                (void) root->db->setMissing({cachedValue->first, name}); // Result intentionally discarded
        }
        return nullptr;
    }

    std::optional<std::pair<AttrId, AttrValue>> cachedValue2;
    if (root->db) {
        if (!cachedValue) {
            if (auto id = root->db->setPlaceholder(getKey()))
                cachedValue = {*id, placeholder_t()};
        }
        if (cachedValue) {
            if (auto id = root->db->setPlaceholder({cachedValue->first, name}))
                cachedValue2 = {*id, placeholder_t()};
        }
    }

    return make_ref<AttrCursor>(
        root, std::make_pair(ref(shared_from_this()), name), attr->value, std::move(cachedValue2));
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(std::string_view name)
{
    return maybeGetAttr(root->state.symbols.create(name));
}

ref<AttrCursor> AttrCursor::getAttr(Symbol name)
{
    auto p = maybeGetAttr(name);
    if (!p)
        throw Error("attribute '%s' does not exist", getAttrPathStr(name));
    return ref(p);
}

ref<AttrCursor> AttrCursor::getAttr(std::string_view name)
{
    return getAttr(root->state.symbols.create(name));
}

OrSuggestions<ref<AttrCursor>> AttrCursor::findAlongAttrPath(const AttrPath & attrPath)
{
    auto res = shared_from_this();
    for (auto & attr : attrPath) {
        auto child = res->maybeGetAttr(attr);
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
        fetchCachedValue();
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

    return v.type() == nString ? std::string(v.string_view()) : v.path().to_string();
}

string_t AttrCursor::getStringWithContext()
{
    if (root->db) {
        fetchCachedValue();
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                bool valid = true;
                for (auto & c : s->second) {
                    const StorePath & path = std::visit(
                        overloaded{
                            [&](const NixStringContextElem::DrvDeep & d) -> const StorePath & { return d.drvPath; },
                            [&](const NixStringContextElem::Built & b) -> const StorePath & {
                                return b.drvPath->getBaseStorePath();
                            },
                            [&](const NixStringContextElem::Opaque & o) -> const StorePath & { return o.path; },
                        },
                        c.raw);
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
        return {std::string{v.string_view()}, std::move(context)};
    } else if (v.type() == nPath)
        return {v.path().to_string(), {}};
    else
        root->state.error<TypeError>("'%s' is not a string but %s", getAttrPathStr(), showType(v)).debugThrow();
}

bool AttrCursor::getBool()
{
    if (root->db) {
        fetchCachedValue();
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

    return v.boolean();
}

NixInt AttrCursor::getInt()
{
    if (root->db) {
        fetchCachedValue();
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

    return v.integer();
}

std::vector<std::string> AttrCursor::getListOfStrings()
{
    if (root->db) {
        fetchCachedValue();
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

    for (auto elem : v.listView())
        res.push_back(
            std::string(root->state.forceStringNoCtx(*elem, noPos, "while evaluating an attribute for caching")));

    if (root->db) {
        if (auto id = root->db->setListOfStrings(getKey(), res))
            cachedValue = {*id, res};
    }

    return res;
}

std::vector<Symbol> AttrCursor::getAttrs()
{
    if (root->db) {
        fetchCachedValue();
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
    for (auto & attr : *getValue().attrs())
        attrs.push_back(attr.name);
    std::sort(attrs.begin(), attrs.end(), [&](Symbol a, Symbol b) {
        std::string_view sa = root->state.symbols[a], sb = root->state.symbols[b];
        return sa < sb;
    });

    if (root->db) {
        if (auto id = root->db->setAttrs(getKey(), attrs))
            cachedValue = {*id, attrs};
    }

    return attrs;
}

bool AttrCursor::isDerivation()
{
    auto aType = maybeGetAttr("type");
    return aType && aType->getString() == "derivation";
}

StorePath AttrCursor::forceDerivation()
{
    auto aDrvPath = getAttr(root->state.s.drvPath);
    auto drvPath = root->state.store->parseStorePath(aDrvPath->getString());
    drvPath.requireDerivation();
    if (!root->state.store->isValidPath(drvPath) && !settings.readOnlyMode) {
        /* The eval cache contains 'drvPath', but the actual path has
           been garbage-collected. So force it to be regenerated. */
        aDrvPath->forceValue();
        if (!root->state.store->isValidPath(drvPath))
            throw Error(
                "don't know how to recreate store derivation '%s'!", root->state.store->printStorePath(drvPath));
    }
    return drvPath;
}

} // namespace nix::eval_cache
