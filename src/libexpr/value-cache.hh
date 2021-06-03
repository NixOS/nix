#pragma once
#include "tree-cache.hh"

namespace nix {
struct Value;
class EvalState;
class Bindings;

class ValueCache {
    tree_cache::Cursor::Ref rawCache;

public:

    ValueCache(tree_cache::Cursor::Ref rawCache) : rawCache(rawCache) {}

    const static ValueCache empty;

    bool isEmpty () { return rawCache == nullptr; }

    enum ReturnCode {
        // The cache result was an attribute set, so we forward it later in the
        // chain
        Forward,
        CacheMiss,
        CacheHit,
        UnCacheable,
        NoCacheKey,
    };

    struct CacheResult {
        ReturnCode returnCode;

        // In case the query returns a `missing_t`, the symbol that's missing
        std::optional<Symbol> lastQueriedSymbolIfMissing;
    };
    std::pair<CacheResult, ValueCache> getValue(EvalState & state, const std::vector<Symbol> & selector, Value & dest);

    ValueCache addChild(const Symbol & attrName, const Value & value);
    ValueCache addFailedChild(const Symbol & attrName, const Error & error);
    ValueCache addNumChild(SymbolTable & symbols, int idx, const Value & value);
    void addAttrSetChilds(Bindings & children);
    void addListChilds(SymbolTable & symbols, Value** elems, int listSize);

    std::optional<std::vector<Symbol>> listChildren(SymbolTable&);
    std::optional<std::vector<Symbol>> listChildrenAtPath(SymbolTable&, const std::vector<Symbol> & attrPath);

    std::optional<tree_cache::AttrValue> getRawValue();

    ValueCache() : rawCache(nullptr) {}
};
}
