#pragma once
///@file

#include "nix/util/sync.hh"
#include "nix/util/hash.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/attr-path.hh"

#include <functional>
#include <variant>

namespace nix::eval_cache {

struct AttrDb;
class AttrCursor;

struct CachedEvalError : CloneableError<CachedEvalError, EvalError>
{
    const ref<AttrCursor> cursor;
    const Symbol attr;

    CachedEvalError(ref<AttrCursor> cursor, Symbol attr);

    /**
     * Evaluate this attribute, which should result in a regular
     * `EvalError` exception being thrown.
     */
    [[noreturn]]
    void force();
};

class EvalCache : public std::enable_shared_from_this<EvalCache>
{
    friend class AttrCursor;
    friend struct CachedEvalError;

    std::shared_ptr<AttrDb> db;
    EvalState & state;
    typedef std::function<Value *()> RootLoader;
    RootLoader rootLoader;
    RootValue value;

    Value * getRootValue();

public:

    EvalCache(std::optional<std::reference_wrapper<const Hash>> useCache, EvalState & state, RootLoader rootLoader);

    ref<AttrCursor> getRoot();
};

enum AttrType {
    Placeholder = 0,
    FullAttrs = 1,
    String = 2,
    Missing = 3,
    Misc = 4,
    Failed = 5,
    Bool = 6,
    ListOfStrings = 7,
    Int = 8,
};

struct placeholder_t
{};

struct missing_t
{};

struct misc_t
{};

struct failed_t
{};

struct int_t
{
    NixInt x;
};

typedef uint64_t AttrId;
typedef std::pair<AttrId, Symbol> AttrKey;
typedef std::pair<std::string, NixStringContext> string_t;

typedef std::variant<
    std::vector<Symbol>,
    string_t,
    placeholder_t,
    missing_t,
    misc_t,
    failed_t,
    bool,
    int_t,
    std::vector<std::string>>
    AttrValue;

class AttrCursor : public std::enable_shared_from_this<AttrCursor>
{
    friend class EvalCache;
    friend struct CachedEvalError;

    ref<EvalCache> root;
    using Parent = std::optional<std::pair<ref<AttrCursor>, Symbol>>;
    Parent parent;
    RootValue _value;
    std::optional<std::pair<AttrId, AttrValue>> cachedValue;

    AttrKey getKey();

    Value & getValue();

    /**
     * If `cachedValue` is unset, try to initialize it from the
     * database. It is not an error if it does not exist. Throw a
     * `CachedEvalError` exception if it does exist but has type
     * `AttrType::Failed`.
     */
    void fetchCachedValue();

public:

    AttrCursor(
        ref<EvalCache> root,
        Parent parent,
        Value * value = nullptr,
        std::optional<std::pair<AttrId, AttrValue>> && cachedValue = {});

    AttrPath getAttrPath() const;

    AttrPath getAttrPath(Symbol name) const;

    std::string getAttrPathStr() const;

    std::string getAttrPathStr(Symbol name) const;

    Suggestions getSuggestionsForAttr(Symbol name);

    std::shared_ptr<AttrCursor> maybeGetAttr(Symbol name);

    std::shared_ptr<AttrCursor> maybeGetAttr(std::string_view name);

    ref<AttrCursor> getAttr(Symbol name);

    ref<AttrCursor> getAttr(std::string_view name);

    /**
     * Get an attribute along a chain of attrsets. Note that this does
     * not auto-call functors or functions.
     */
    OrSuggestions<ref<AttrCursor>> findAlongAttrPath(const AttrPath & attrPath);

    std::string getString();

    string_t getStringWithContext();

    bool getBool();

    NixInt getInt();

    std::vector<std::string> getListOfStrings();

    std::vector<Symbol> getAttrs();

    bool isDerivation();

    Value & forceValue();

    /**
     * Force creation of the .drv file in the Nix store.
     */
    StorePath forceDerivation();
};

} // namespace nix::eval_cache
