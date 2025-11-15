#pragma once
///@file

#include "nix/expr/attr-set.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/eval-profiler.hh"
#include "nix/util/types.hh"
#include "nix/expr/value.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/util/configuration.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/position.hh"
#include "nix/util/pos-table.hh"
#include "nix/util/source-accessor.hh"
#include "nix/expr/search-path.hh"
#include "nix/expr/repl-exit-status.hh"
#include "nix/util/ref.hh"
#include "nix/expr/counter.hh"

// For `NIX_USE_BOEHMGC`, and if that's set, `GC_THREADS`
#include "nix/expr/config.hh"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/concurrent_flat_map_fwd.hpp>

#include <map>
#include <optional>
#include <functional>

namespace nix {

/**
 * We put a limit on primop arity because it lets us use a fixed size array on
 * the stack. 8 is already an impractical number of arguments. Use an attrset
 * argument for such overly complicated functions.
 */
constexpr size_t maxPrimOpArity = 8;

class Store;

namespace fetchers {
struct Settings;
struct InputCache;
struct Input;
} // namespace fetchers
struct EvalSettings;
class EvalState;
class StorePath;
struct SingleDerivedPath;
enum RepairFlag : bool;
struct MemorySourceAccessor;
struct MountedSourceAccessor;

namespace eval_cache {
class EvalCache;
}

/**
 * Increments a count on construction and decrements on destruction.
 */
class CallDepth
{
    size_t & count;

public:
    CallDepth(size_t & count)
        : count(count)
    {
        ++count;
    }

    ~CallDepth()
    {
        --count;
    }
};

/**
 * Function that implements a primop.
 */
using PrimOpFun = void(EvalState & state, const PosIdx pos, Value ** args, Value & v);

/**
 * Info about a primitive operation, and its implementation
 */
struct PrimOp
{
    /**
     * Name of the primop. `__` prefix is treated specially.
     */
    std::string name;

    /**
     * Names of the parameters of a primop, for primops that take a
     * fixed number of arguments to be substituted for these parameters.
     */
    std::vector<std::string> args;

    /**
     * Aritiy of the primop.
     *
     * If `args` is not empty, this field will be computed from that
     * field instead, so it doesn't need to be manually set.
     */
    size_t arity = 0;

    /**
     * Optional free-form documentation about the primop.
     */
    const char * doc = nullptr;

    /**
     * Add a trace item, while calling the `<name>` builtin.
     *
     * This is used to remove the redundant item for `builtins.addErrorContext`.
     */
    bool addTrace = true;

    /**
     * Implementation of the primop.
     */
    std::function<PrimOpFun> fun;

    /**
     * Optional experimental for this to be gated on.
     */
    std::optional<ExperimentalFeature> experimentalFeature;

    /**
     * If true, this primop is not exposed to the user.
     */
    bool internal = false;

    /**
     * Validity check to be performed by functions that introduce primops,
     * such as RegisterPrimOp() and Value::mkPrimOp().
     */
    void check();
};

std::ostream & operator<<(std::ostream & output, const PrimOp & primOp);

/**
 * Info about a constant
 */
struct Constant
{
    /**
     * Optional type of the constant (known since it is a fixed value).
     *
     * @todo we should use an enum for this.
     */
    ValueType type = nThunk;

    /**
     * Optional free-form documentation about the constant.
     */
    const char * doc = nullptr;

    /**
     * Whether the constant is impure, and not available in pure mode.
     */
    bool impureOnly = false;
};

typedef std::
    map<std::string, Value *, std::less<std::string>, traceable_allocator<std::pair<const std::string, Value *>>>
        ValMap;

typedef boost::unordered_flat_map<PosIdx, DocComment, std::hash<PosIdx>> DocCommentMap;

struct Env
{
    Env * up;
    Value * values[0];
};

void printEnvBindings(const EvalState & es, const Expr & expr, const Env & env);
void printEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, int lvl = 0);

std::unique_ptr<ValMap> mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env);

void copyContext(
    const Value & v,
    NixStringContext & context,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

std::string printValue(EvalState & state, Value & v);
std::ostream & operator<<(std::ostream & os, const ValueType t);

struct RegexCache;

ref<RegexCache> makeRegexCache();

struct DebugTrace
{
    /* WARNING: Converting PosIdx -> Pos should be done with extra care. This is
       due to the fact that operator[] of PosTable is incredibly expensive. */
    std::variant<Pos, PosIdx> pos;
    const Expr & expr;
    const Env & env;
    HintFmt hint;
    bool isError;

    Pos getPos(const PosTable & table) const
    {
        return std::visit(
            overloaded{
                [&](PosIdx idx) {
                    // Prefer direct pos, but if noPos then try the expr.
                    if (!idx)
                        idx = expr.getPos();
                    return table[idx];
                },
                [&](Pos pos) { return pos; },
            },
            pos);
    }
};

struct StaticEvalSymbols
{
    Symbol with, outPath, drvPath, type, meta, name, value, system, overrides, outputs, outputName, ignoreNulls, file,
        line, column, functor, toString, right, wrong, structuredAttrs, json, allowedReferences, allowedRequisites,
        disallowedReferences, disallowedRequisites, maxSize, maxClosureSize, builder, args, contentAddressed, impure,
        outputHash, outputHashAlgo, outputHashMode, recurseForDerivations, description, self, epsilon, startSet,
        operator_, key, path, prefix, outputSpecified;

    Expr::AstSymbols exprSymbols;

    static constexpr auto preallocate()
    {
        StaticSymbolTable alloc;

        StaticEvalSymbols staticSymbols = {
            .with = alloc.create("<with>"),
            .outPath = alloc.create("outPath"),
            .drvPath = alloc.create("drvPath"),
            .type = alloc.create("type"),
            .meta = alloc.create("meta"),
            .name = alloc.create("name"),
            .value = alloc.create("value"),
            .system = alloc.create("system"),
            .overrides = alloc.create("__overrides"),
            .outputs = alloc.create("outputs"),
            .outputName = alloc.create("outputName"),
            .ignoreNulls = alloc.create("__ignoreNulls"),
            .file = alloc.create("file"),
            .line = alloc.create("line"),
            .column = alloc.create("column"),
            .functor = alloc.create("__functor"),
            .toString = alloc.create("__toString"),
            .right = alloc.create("right"),
            .wrong = alloc.create("wrong"),
            .structuredAttrs = alloc.create("__structuredAttrs"),
            .json = alloc.create("__json"),
            .allowedReferences = alloc.create("allowedReferences"),
            .allowedRequisites = alloc.create("allowedRequisites"),
            .disallowedReferences = alloc.create("disallowedReferences"),
            .disallowedRequisites = alloc.create("disallowedRequisites"),
            .maxSize = alloc.create("maxSize"),
            .maxClosureSize = alloc.create("maxClosureSize"),
            .builder = alloc.create("builder"),
            .args = alloc.create("args"),
            .contentAddressed = alloc.create("__contentAddressed"),
            .impure = alloc.create("__impure"),
            .outputHash = alloc.create("outputHash"),
            .outputHashAlgo = alloc.create("outputHashAlgo"),
            .outputHashMode = alloc.create("outputHashMode"),
            .recurseForDerivations = alloc.create("recurseForDerivations"),
            .description = alloc.create("description"),
            .self = alloc.create("self"),
            .epsilon = alloc.create(""),
            .startSet = alloc.create("startSet"),
            .operator_ = alloc.create("operator"),
            .key = alloc.create("key"),
            .path = alloc.create("path"),
            .prefix = alloc.create("prefix"),
            .outputSpecified = alloc.create("outputSpecified"),
            .exprSymbols = {
                .sub = alloc.create("__sub"),
                .lessThan = alloc.create("__lessThan"),
                .mul = alloc.create("__mul"),
                .div = alloc.create("__div"),
                .or_ = alloc.create("or"),
                .findFile = alloc.create("__findFile"),
                .nixPath = alloc.create("__nixPath"),
                .body = alloc.create("body"),
            }};

        return std::pair{staticSymbols, alloc};
    }

    static consteval StaticEvalSymbols create()
    {
        return preallocate().first;
    }

    static constexpr StaticSymbolTable staticSymbolTable()
    {
        return preallocate().second;
    }
};

class EvalMemory
{
#if NIX_USE_BOEHMGC
    /**
     * Allocation cache for GC'd Value objects.
     */
    std::shared_ptr<void *> valueAllocCache;

    /**
     * Allocation cache for size-1 Env objects.
     */
    std::shared_ptr<void *> env1AllocCache;
#endif

public:
    struct Statistics
    {
        Counter nrEnvs;
        Counter nrValuesInEnvs;
        Counter nrValues;
        Counter nrAttrsets;
        Counter nrAttrsInAttrsets;
        Counter nrListElems;
    };

    EvalMemory();

    EvalMemory(const EvalMemory &) = delete;
    EvalMemory(EvalMemory &&) = delete;
    EvalMemory & operator=(const EvalMemory &) = delete;
    EvalMemory & operator=(EvalMemory &&) = delete;

    inline void * allocBytes(size_t n);
    inline Value * allocValue();
    inline Env & allocEnv(size_t size);

    Bindings * allocBindings(size_t capacity);

    BindingsBuilder buildBindings(SymbolTable & symbols, size_t capacity)
    {
        return BindingsBuilder(*this, symbols, allocBindings(capacity), capacity);
    }

    ListBuilder buildList(size_t size)
    {
        stats.nrListElems += size;
        return ListBuilder(*this, size);
    }

    const Statistics & getStats() const &
    {
        return stats;
    }

    /**
     * Storage for the AST nodes
     */
    Exprs exprs;

private:
    Statistics stats;
};

class EvalState : public std::enable_shared_from_this<EvalState>
{
public:
    static constexpr StaticEvalSymbols s = StaticEvalSymbols::create();

    const fetchers::Settings & fetchSettings;
    const EvalSettings & settings;

    SymbolTable symbols;
    PosTable positions;

    EvalMemory mem;

    /**
     * If set, force copying files to the Nix store even if they
     * already exist there.
     */
    RepairFlag repair;

    /**
     * The accessor corresponding to `store`.
     */
    const ref<MountedSourceAccessor> storeFS;

    /**
     * The accessor for the root filesystem.
     */
    const ref<SourceAccessor> rootFS;

    /**
     * The in-memory filesystem for <nix/...> paths.
     */
    const ref<MemorySourceAccessor> corepkgsFS;

    /**
     * In-memory filesystem for internal, non-user-callable Nix
     * expressions like `derivation.nix`.
     */
    const ref<MemorySourceAccessor> internalFS;

    const SourcePath derivationInternal;

    /**
     * Store used to materialise .drv files.
     */
    const ref<Store> store;

    /**
     * Store used to build stuff.
     */
    const ref<Store> buildStore;

    RootValue vImportedDrvToDerivation = nullptr;

    const ref<fetchers::InputCache> inputCache;

    /**
     * Debugger
     */
    ReplExitStatus (*debugRepl)(ref<EvalState> es, const ValMap & extraEnv);
    bool debugStop;
    bool inDebugger = false;
    int trylevel;
    std::list<DebugTrace> debugTraces;
    boost::unordered_flat_map<const Expr *, const std::shared_ptr<const StaticEnv>> exprEnvs;

    const std::shared_ptr<const StaticEnv> getStaticEnv(const Expr & expr) const
    {
        auto i = exprEnvs.find(&expr);
        if (i != exprEnvs.end())
            return i->second;
        else
            return std::shared_ptr<const StaticEnv>();
        ;
    }

    /** Whether a debug repl can be started. If `false`, `runDebugRepl(error)` will return without starting a repl. */
    bool canDebug();

    /** Use front of `debugTraces`; see `runDebugRepl(error,env,expr)` */
    void runDebugRepl(const Error * error);

    /**
     * Run a debug repl with the given error, environment and expression.
     * @param error The error to debug, may be nullptr.
     * @param env The environment to debug, matching the expression.
     * @param expr The expression to debug, matching the environment.
     */
    void runDebugRepl(const Error * error, const Env & env, const Expr & expr);

    template<class T, typename... Args>
    [[nodiscard, gnu::noinline]]
    EvalErrorBuilder<T> & error(const Args &... args)
    {
        // `EvalErrorBuilder::debugThrow` performs the corresponding `delete`.
        return *new EvalErrorBuilder<T>(*this, args...);
    }

    /**
     * A cache for evaluation caches, so as to reuse the same root value if possible
     */
    std::map<const Hash, ref<eval_cache::EvalCache>> evalCaches;

private:

    /* Cache for calls to addToStore(); maps source paths to the store
       paths. */
    const ref<boost::concurrent_flat_map<SourcePath, StorePath>> srcToStore;

    /**
     * A cache that maps paths to "resolved" paths for importing Nix
     * expressions, i.e. `/foo` to `/foo/default.nix`.
     */
    const ref<boost::concurrent_flat_map<SourcePath, SourcePath>> importResolutionCache;

    /**
     * A cache from resolved paths to values.
     */
    const ref<boost::concurrent_flat_map<
        SourcePath,
        Value *,
        std::hash<SourcePath>,
        std::equal_to<SourcePath>,
        traceable_allocator<std::pair<const SourcePath, Value *>>>>
        fileEvalCache;

    /**
     * Associate source positions of certain AST nodes with their preceding doc comment, if they have one.
     * Grouped by file.
     */
    boost::unordered_flat_map<SourcePath, DocCommentMap> positionToDocComment;

    LookupPath lookupPath;

    boost::unordered_flat_map<std::string, std::optional<SourcePath>, StringViewHash, std::equal_to<>>
        lookupPathResolved;

    /**
     * Cache used by prim_match().
     */
    const ref<RegexCache> regexCache;

public:

    /**
     * @param lookupPath     Only used during construction.
     * @param store          The store to use for instantiation
     * @param fetchSettings  Must outlive the lifetime of this EvalState!
     * @param settings       Must outlive the lifetime of this EvalState!
     * @param buildStore     The store to use for builds ("import from derivation", C API `nix_string_realise`)
     */
    EvalState(
        const LookupPath & lookupPath,
        ref<Store> store,
        const fetchers::Settings & fetchSettings,
        const EvalSettings & settings,
        std::shared_ptr<Store> buildStore = nullptr);
    ~EvalState();

    /**
     * A wrapper around EvalMemory::allocValue() to avoid code churn when it
     * was introduced.
     */
    inline Value * allocValue()
    {
        return mem.allocValue();
    }

    LookupPath getLookupPath()
    {
        return lookupPath;
    }

    /**
     * Return a `SourcePath` that refers to `path` in the root
     * filesystem.
     */
    SourcePath rootPath(CanonPath path);

    /**
     * Variant which accepts relative paths too.
     */
    SourcePath rootPath(PathView path);

    /**
     * Return a `SourcePath` that refers to `path` in the store.
     *
     * For now, this has to also be within the root filesystem for
     * backwards compat, but for Windows and maybe also pure eval, we'll
     * probably want to do something different.
     */
    SourcePath storePath(const StorePath & path);

    /**
     * Allow access to a path.
     *
     * Only for restrict eval: pure eval just whitelist store paths,
     * never arbitrary paths.
     */
    void allowPathLegacy(const Path & path);

    /**
     * Allow access to a store path. Note that this gets remapped to
     * the real store path if `store` is a chroot store.
     */
    void allowPath(const StorePath & storePath);

    /**
     * Allow access to the closure of a store path.
     */
    void allowClosure(const StorePath & storePath);

    /**
     * Allow access to a store path and return it as a string.
     */
    void allowAndSetStorePathString(const StorePath & storePath, Value & v);

    void checkURI(const std::string & uri);

    /**
     * Mount an input on the Nix store.
     */
    StorePath mountInput(fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor);

    /**
     * Parse a Nix expression from the specified file.
     */
    Expr * parseExprFromFile(const SourcePath & path);
    Expr * parseExprFromFile(const SourcePath & path, const std::shared_ptr<StaticEnv> & staticEnv);

    /**
     * Parse a Nix expression from the specified string.
     */
    Expr *
    parseExprFromString(std::string s, const SourcePath & basePath, const std::shared_ptr<StaticEnv> & staticEnv);
    Expr * parseExprFromString(std::string s, const SourcePath & basePath);

    Expr * parseStdin();

    /**
     * Evaluate an expression read from the given file to normal
     * form. Optionally enforce that the top-level expression is
     * trivial (i.e. doesn't require arbitrary computation).
     */
    void evalFile(const SourcePath & path, Value & v, bool mustBeTrivial = false);

    void resetFileCache();

    /**
     * Look up a file in the search path.
     */
    SourcePath findFile(const std::string_view path);
    SourcePath findFile(const LookupPath & lookupPath, const std::string_view path, const PosIdx pos = noPos);

    /**
     * Try to resolve a search path value (not the optional key part).
     *
     * If the specified search path element is a URI, download it.
     *
     * If it is not found, return `std::nullopt`.
     */
    std::optional<SourcePath> resolveLookupPathPath(const LookupPath::Path & elem, bool initAccessControl = false);

    /**
     * Evaluate an expression to normal form
     *
     * @param [out] v The resulting is stored here.
     */
    void eval(Expr * e, Value & v);

    /**
     * Evaluation the expression, then verify that it has the expected
     * type.
     */
    inline bool evalBool(Env & env, Expr * e);
    inline bool evalBool(Env & env, Expr * e, const PosIdx pos, std::string_view errorCtx);
    inline void evalAttrs(Env & env, Expr * e, Value & v, const PosIdx pos, std::string_view errorCtx);

    /**
     * If `v` is a thunk, enter it and overwrite `v` with the result
     * of the evaluation of the thunk.  If `v` is a delayed function
     * application, call the function and overwrite `v` with the
     * result.  Otherwise, this is a no-op.
     */
    inline void forceValue(Value & v, const PosIdx pos);

    void tryFixupBlackHolePos(Value & v, PosIdx pos);

    /**
     * Force a value, then recursively force list elements and
     * attributes.
     */
    void forceValueDeep(Value & v);

    /**
     * Force `v`, and then verify that it has the expected type.
     */
    NixInt forceInt(Value & v, const PosIdx pos, std::string_view errorCtx);
    NixFloat forceFloat(Value & v, const PosIdx pos, std::string_view errorCtx);
    bool forceBool(Value & v, const PosIdx pos, std::string_view errorCtx);

    void forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx);

    template<typename Callable>
    inline void forceAttrs(Value & v, Callable getPos, std::string_view errorCtx);

    inline void forceList(Value & v, const PosIdx pos, std::string_view errorCtx);
    /**
     * @param v either lambda or primop
     */
    void forceFunction(Value & v, const PosIdx pos, std::string_view errorCtx);
    std::string_view forceString(Value & v, const PosIdx pos, std::string_view errorCtx);
    std::string_view forceString(
        Value & v,
        NixStringContext & context,
        const PosIdx pos,
        std::string_view errorCtx,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
    std::string_view forceStringNoCtx(Value & v, const PosIdx pos, std::string_view errorCtx);

    /**
     * Get attribute from an attribute set and throw an error if it doesn't exist.
     */
    const Attr * getAttr(Symbol attrSym, const Bindings * attrSet, std::string_view errorCtx);

    template<typename... Args>
    [[gnu::noinline]]
    void addErrorTrace(Error & e, const Args &... formatArgs) const;
    template<typename... Args>
    [[gnu::noinline]]
    void addErrorTrace(Error & e, const PosIdx pos, const Args &... formatArgs) const;

public:
    /**
     * @return true iff the value `v` denotes a derivation (i.e. a
     * set with attribute `type = "derivation"`).
     */
    bool isDerivation(Value & v);

    std::optional<std::string> tryAttrsToString(
        const PosIdx pos, Value & v, NixStringContext & context, bool coerceMore = false, bool copyToStore = true);

    /**
     * String coercion.
     *
     * Converts strings, paths and derivations to a
     * string.  If `coerceMore` is set, also converts nulls, integers,
     * booleans and lists to a string.  If `copyToStore` is set,
     * referenced paths are copied to the Nix store as a side effect.
     */
    BackedStringView coerceToString(
        const PosIdx pos,
        Value & v,
        NixStringContext & context,
        std::string_view errorCtx,
        bool coerceMore = false,
        bool copyToStore = true,
        bool canonicalizePath = true);

    StorePath copyPathToStore(NixStringContext & context, const SourcePath & path);

    /**
     * Path coercion.
     *
     * Converts strings, paths and derivations to a
     * path.  The result is guaranteed to be a canonicalised, absolute
     * path.  Nothing is copied to the store.
     */
    SourcePath coerceToPath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx);

    /**
     * Like coerceToPath, but the result must be a store path.
     */
    StorePath coerceToStorePath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx);

    /**
     * Part of `coerceToSingleDerivedPath()` without any store IO which is exposed for unit testing only.
     */
    std::pair<SingleDerivedPath, std::string_view> coerceToSingleDerivedPathUnchecked(
        const PosIdx pos,
        Value & v,
        std::string_view errorCtx,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    /**
     * Coerce to `SingleDerivedPath`.
     *
     * Must be a string which is either a literal store path or a
     * "placeholder (see `DownstreamPlaceholder`).
     *
     * Even more importantly, the string context must be exactly one
     * element, which is either a `NixStringContextElem::Opaque` or
     * `NixStringContextElem::Built`. (`NixStringContextEleme::DrvDeep`
     * is not permitted).
     *
     * The string is parsed based on the context --- the context is the
     * source of truth, and ultimately tells us what we want, and then
     * we ensure the string corresponds to it.
     */
    SingleDerivedPath coerceToSingleDerivedPath(const PosIdx pos, Value & v, std::string_view errorCtx);

#if NIX_USE_BOEHMGC
    /** A GC root for the baseEnv reference. */
    const std::shared_ptr<Env *> baseEnvP;
#endif

public:

    /**
     * The base environment, containing the builtin functions and
     * values.
     */
    Env & baseEnv;

    /**
     * The same, but used during parsing to resolve variables.
     */
    const std::shared_ptr<StaticEnv> staticBaseEnv; // !!! should be private

    /**
     * Internal primops not exposed to the user.
     */
    boost::unordered_flat_map<
        std::string,
        Value *,
        StringViewHash,
        std::equal_to<>,
        traceable_allocator<std::pair<const std::string, Value *>>>
        internalPrimOps;

    /**
     * Name and documentation about every constant.
     *
     * Constants from primops are hard to crawl, and their docs will go
     * here too.
     */
    std::vector<std::pair<std::string, Constant>> constantInfos;

private:

    unsigned int baseEnvDispl = 0;

    void createBaseEnv(const EvalSettings & settings);

    Value * addConstant(const std::string & name, Value & v, Constant info);

    void addConstant(const std::string & name, Value * v, Constant info);

    Value * addPrimOp(PrimOp && primOp);

public:

    /**
     * Retrieve a specific builtin, equivalent to evaluating `builtins.${name}`.
     * @param name The attribute name of the builtin to retrieve.
     * @throws EvalError if the builtin does not exist.
     */
    Value & getBuiltin(const std::string & name);

    /**
     * Retrieve the `builtins` attrset, equivalent to evaluating the reference `builtins`.
     * Always returns an attribute set value.
     */
    Value & getBuiltins();

    struct Doc
    {
        Pos pos;
        std::optional<std::string> name;
        size_t arity;
        std::vector<std::string> args;
        /**
         * Unlike the other `doc` fields in this file, this one should never be
         * `null`.
         */
        const char * doc;
    };

    /**
     * Retrieve the documentation for a value. This will evaluate the value if
     * it is a thunk, and it will partially apply __functor if applicable.
     *
     * @param v The value to get the documentation for.
     */
    std::optional<Doc> getDoc(Value & v);

private:

    inline Value * lookupVar(Env * env, const ExprVar & var, bool noEval);

    friend struct ExprVar;
    friend struct ExprAttrs;
    friend struct ExprLet;

    Expr * parse(
        char * text,
        size_t length,
        Pos::Origin origin,
        const SourcePath & basePath,
        const std::shared_ptr<StaticEnv> & staticEnv);

    /**
     * Current Nix call stack depth, used with `max-call-depth` setting to throw stack overflow hopefully before we run
     * out of system stack.
     */
    size_t callDepth = 0;

public:

    /**
     * Check that the call depth is within limits, and increment it, until the returned object is destroyed.
     */
    inline CallDepth addCallDepth(const PosIdx pos);

    /**
     * Do a deep equality test between two values.  That is, list
     * elements and attributes are compared recursively.
     */
    bool eqValues(Value & v1, Value & v2, const PosIdx pos, std::string_view errorCtx);

    /**
     * Like `eqValues`, but throws an `AssertionError` if not equal.
     *
     * WARNING:
     * Callers should call `eqValues` first and report if `assertEqValues` behaves
     * incorrectly. (e.g. if it doesn't throw if eqValues returns false or vice versa)
     */
    void assertEqValues(Value & v1, Value & v2, const PosIdx pos, std::string_view errorCtx);

    bool isFunctor(const Value & fun) const;

    void callFunction(Value & fun, std::span<Value *> args, Value & vRes, const PosIdx pos);

    void callFunction(Value & fun, Value & arg, Value & vRes, const PosIdx pos)
    {
        Value * args[] = {&arg};
        callFunction(fun, args, vRes, pos);
    }

    /**
     * Automatically call a function for which each argument has a
     * default value or has a binding in the `args` map.
     */
    void autoCallFunction(const Bindings & args, Value & fun, Value & res);

    BindingsBuilder buildBindings(size_t capacity)
    {
        return mem.buildBindings(symbols, capacity);
    }

    ListBuilder buildList(size_t size)
    {
        return mem.buildList(size);
    }

    /**
     * Return a boolean `Value *` without allocating.
     */
    Value * getBool(bool b);

    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, PosIdx pos);

    /**
     * Create a string representing a store path.
     *
     * The string is the printed store path with a context containing a
     * single `NixStringContextElem::Opaque` element of that store path.
     */
    void mkStorePathString(const StorePath & storePath, Value & v);

    /**
     * Create a string representing a `SingleDerivedPath::Built`.
     *
     * The string is the printed store path with a context containing a
     * single `NixStringContextElem::Built` element of the drv path and
     * output name.
     *
     * @param value Value we are settings
     *
     * @param b the drv whose output we are making a string for, and the
     * output
     *
     * @param optStaticOutputPath Optional output path for that string.
     * Must be passed if and only if output store object is
     * input-addressed or fixed output. Will be printed to form string
     * if passed, otherwise a placeholder will be used (see
     * `DownstreamPlaceholder`).
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    void mkOutputString(
        Value & value,
        const SingleDerivedPath::Built & b,
        std::optional<StorePath> optStaticOutputPath,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    /**
     * Create a string representing a `SingleDerivedPath`.
     *
     * A combination of `mkStorePathString` and `mkOutputString`.
     */
    void mkSingleDerivedPathString(const SingleDerivedPath & p, Value & v);

    void concatLists(Value & v, size_t nrLists, Value * const * lists, const PosIdx pos, std::string_view errorCtx);

    /**
     * Print statistics, if enabled.
     *
     * Performs a full memory GC before printing the statistics, so that the
     * GC statistics are more accurate.
     */
    void maybePrintStats();

    /**
     * Print statistics, unconditionally, cheaply, without performing a GC first.
     */
    void printStatistics();

    /**
     * Perform a full memory garbage collection - not incremental.
     *
     * @return true if Nix was built with GC and a GC was performed, false if not.
     *              The return value is currently not thread safe - just the return value.
     */
    bool fullGC();

    /**
     * Realise the given context
     * @param[in] context the context to realise
     * @param[out] maybePaths if not nullptr, all built or referenced store paths will be added to this set
     * @return a mapping from the placeholders used to construct the associated value to their final store path.
     */
    [[nodiscard]] StringMap
    realiseContext(const NixStringContext & context, StorePathSet * maybePaths = nullptr, bool isIFD = true);

    /**
     * Realise the given string with context, and return the string with outputs instead of downstream output
     * placeholders.
     * @param[in] str the string to realise
     * @param[out] paths all referenced store paths will be added to this set
     * @return the realised string
     * @throw EvalError if the value is not a string, path or derivation (see `coerceToString`)
     */
    std::string
    realiseString(Value & str, StorePathSet * storePathsOutMaybe, bool isIFD = true, const PosIdx pos = noPos);

    /* Call the binary path filter predicate used builtins.path etc. */
    bool callPathFilter(Value * filterFun, const SourcePath & path, PosIdx pos);

    DocComment getDocCommentForPos(PosIdx pos);

private:

    /**
     * Like `mkOutputString` but just creates a raw string, not an
     * string Value, which would also have a string context.
     */
    std::string mkOutputStringRaw(
        const SingleDerivedPath::Built & b,
        std::optional<StorePath> optStaticOutputPath,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    /**
     * Like `mkSingleDerivedPathStringRaw` but just creates a raw string
     * Value, which would also have a string context.
     */
    std::string mkSingleDerivedPathStringRaw(const SingleDerivedPath & p);

    Counter nrLookups;
    Counter nrAvoided;
    Counter nrOpUpdates;
    Counter nrOpUpdateValuesCopied;
    Counter nrListConcats;
    Counter nrPrimOpCalls;
    Counter nrFunctionCalls;

    bool countCalls;

    typedef boost::unordered_flat_map<std::string, size_t, StringViewHash, std::equal_to<>> PrimOpCalls;
    PrimOpCalls primOpCalls;

    typedef boost::unordered_flat_map<ExprLambda *, size_t> FunctionCalls;
    FunctionCalls functionCalls;

    /** Evaluation/call profiler. */
    MultiEvalProfiler profiler;

    void incrFunctionCall(ExprLambda * fun);

    typedef boost::unordered_flat_map<PosIdx, size_t, std::hash<PosIdx>> AttrSelects;
    AttrSelects attrSelects;

    friend struct ExprOpUpdate;
    friend struct ExprOpConcatLists;
    friend struct ExprVar;
    friend struct ExprString;
    friend struct ExprInt;
    friend struct ExprFloat;
    friend struct ExprPath;
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const PosIdx pos, Value ** args, Value & v);
    friend void prim_match(EvalState & state, const PosIdx pos, Value ** args, Value & v);
    friend void prim_split(EvalState & state, const PosIdx pos, Value ** args, Value & v);

    friend struct Value;
    friend class ListBuilder;
};

struct DebugTraceStacker
{
    DebugTraceStacker(EvalState & evalState, DebugTrace t);

    ~DebugTraceStacker()
    {
        evalState.debugTraces.pop_front();
    }

    EvalState & evalState;
    DebugTrace trace;
};

/**
 * @return A string representing the type of the value `v`.
 *
 * @param withArticle Whether to begin with an english article, e.g. "an
 * integer" vs "integer".
 */
std::string_view showType(ValueType type, bool withArticle = true);
std::string showType(const Value & v);

/**
 * If `path` refers to a directory, then append "/default.nix".
 *
 * @param addDefaultNix Whether to append "/default.nix" after resolving symlinks.
 */
SourcePath resolveExprPath(SourcePath path, bool addDefaultNix = true);

/**
 * Whether a URI is allowed, assuming restrictEval is enabled
 */
bool isAllowedURI(std::string_view uri, const Strings & allowedPaths);

} // namespace nix

#include "nix/expr/eval-inline.hh"
