#pragma once
///@file

#include "sync.hh"
#include "hash.hh"
#include "eval.hh"

#include <functional>
#include <variant>

namespace nix::eval_cache {

MakeError(CachedEvalError, EvalError);

struct AttrDb;
class AttrCursor;

class EvalCache : public std::enable_shared_from_this<EvalCache>
{
    friend class AttrCursor;

    std::shared_ptr<AttrDb> db;
    EvalState & state;
    typedef std::function<Value *()> RootLoader;
    RootLoader rootLoader;
    RootValue value;

    Value * getRootValue();

public:

    EvalCache(
        std::optional<std::reference_wrapper<const Hash>> useCache,
        EvalState & state,
        RootLoader rootLoader);

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

struct placeholder_t {};
struct missing_t {};
struct misc_t {};
struct failed_t {};
struct int_t { NixInt x; };
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
    std::vector<std::string>
    > AttrValue;

class AttrCursor : public std::enable_shared_from_this<AttrCursor>
{
    friend class EvalCache;

    ref<EvalCache> root;
    typedef std::optional<std::pair<std::shared_ptr<AttrCursor>, Symbol>> Parent;
    Parent parent;
    RootValue _value;
    std::optional<std::pair<AttrId, AttrValue>> cachedValue;

    AttrKey getKey();

    Value & getValue();

public:

    AttrCursor(
        ref<EvalCache> root,
        Parent parent,
        Value * value = nullptr,
        std::optional<std::pair<AttrId, AttrValue>> && cachedValue = {});

    std::vector<Symbol> getAttrPath() const;

    std::vector<Symbol> getAttrPath(Symbol name) const;

    std::string getAttrPathStr() const;

    std::string getAttrPathStr(Symbol name) const;

    Suggestions getSuggestionsForAttr(Symbol name);

    std::shared_ptr<AttrCursor> maybeGetAttr(Symbol name, bool forceErrors = false);

    std::shared_ptr<AttrCursor> maybeGetAttr(std::string_view name);

    ref<AttrCursor> getAttr(Symbol name, bool forceErrors = false);

    ref<AttrCursor> getAttr(std::string_view name);

    /**
     * Get an attribute along a chain of attrsets. Note that this does
     * not auto-call functors or functions.
     */
    OrSuggestions<ref<AttrCursor>> findAlongAttrPath(const std::vector<Symbol> & attrPath, bool force = false);

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

}
