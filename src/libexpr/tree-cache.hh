/**
 * caching for a tree-like data structure (like Nix values)
 *
 * The cache is an sqlite db whose rows are the nodes of the tree, with a
 * pointer to their parent (except for the root of course)
 */

#pragma once

#include "sync.hh"
#include "hash.hh"
#include "symbol-table.hh"

#include <functional>
#include <variant>

namespace nix::tree_cache {

struct AttrDb;
class Cursor;

class Cache : public std::enable_shared_from_this<Cache>
{
private:
    friend class Cursor;

    /**
     * The database holding the cache
     */
    std::shared_ptr<AttrDb> db;

    SymbolTable & symbols;

    /**
     * Distinguished symbol indicating the root of the tree
     */
    const Symbol rootSymbol;

public:

    Cache(
        const Hash & useCache,
        SymbolTable & symbols
    );

    static std::shared_ptr<Cache> tryCreate(const Hash & useCache, SymbolTable & symbols);

    Cursor * getRoot();

    /**
     * Flush the cache to disk
     */
    void commit();
};

enum AttrType {
    Unknown = 0,
    Attrs = 1,
    String = 2,
    Bool = 3,
    Int = 4,
    Double = 5,
    Thunk = 6,
    Missing = 7, // Missing fields of attribute sets
    Failed = 8,
};

struct attributeSet_t {};
struct unknown_t {};
struct thunk_t {};
struct failed_t { string error; };
struct missing_t { Symbol attrName; };

// Putting several different primitive types in an `std::variant` partially
// breaks the `std::visit(overloaded{...` hackery because of the implicit cast
// from one to another which breaks the exhaustiveness check.
// So we wrap them in a trivial class just to force the disambiguation
template<typename T>
struct wrapped_basetype{ T value; };

typedef uint64_t AttrId;

typedef std::pair<AttrId, Symbol> AttrKey;
typedef std::pair<std::string, std::vector<std::pair<Path, std::string>>> string_t;

typedef std::variant<
    attributeSet_t,
    string_t,
    unknown_t,
    thunk_t,
    missing_t,
    failed_t,
    wrapped_basetype<bool>,
    wrapped_basetype<int64_t>,
    wrapped_basetype<double>
> AttrValue;

struct RawValue {
    AttrType type;
    std::optional<std::string> value;
    std::vector<std::pair<Path, std::string>> context;

    std::string serializeContext() const;

    static const RawValue fromVariant(const AttrValue&);
    AttrValue toVariant() const;
};

/**
 * View inside the cache.
 *
 * A `Cursor` represents a node in the cached tree (be it a leaf or not)
 */
class Cursor : public std::enable_shared_from_this<Cursor>
{
    /**
     * The overall cache of which this cursor is a view
     */
    ref<Cache> root;

    typedef std::optional<std::pair<Cursor&, Symbol>> Parent;

    std::optional<AttrId> parentId;
    Symbol label;

    std::pair<AttrId, AttrValue> cachedValue;

    /**
     * Get the identifier for this node in the database
     */
    AttrKey getKey();

public:

    using Ref = Cursor*;

    // Create a new cache entry
    Cursor(ref<Cache> root, const Parent & parent, const AttrValue&);
    // Build a cursor from an existing cache entry
    Cursor(ref<Cache> root, const Parent & parent, const AttrId& id, const AttrValue&);

    AttrValue getCachedValue();

    void setValue(const AttrValue & v);

    Ref addChild(const Symbol & attrPath, const AttrValue & v);

    Ref findAlongAttrPath(const std::vector<Symbol> & attrPath);
    Ref maybeGetAttr(const Symbol & attrPath);


    std::vector<std::string> getChildren();
    std::optional<std::vector<std::string>> getChildrenAtPath(const std::vector<Symbol> & attrPath);
};

}
