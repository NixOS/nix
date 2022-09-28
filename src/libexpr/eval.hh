#pragma once

#include "attr-set.hh"
#include "types.hh"
#include "value.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "config.hh"
#include "experimental-features.hh"
#include "input-accessor.hh"

#include <map>
#include <optional>
#include <unordered_map>
#include <mutex>

namespace nix {


class Store;
class EvalState;
class StorePath;
struct SourcePath;
enum RepairFlag : bool;
struct FSInputAccessor;


typedef void (* PrimOpFun) (EvalState & state, const PosIdx pos, Value * * args, Value & v);

struct PrimOp
{
    PrimOpFun fun;
    size_t arity;
    std::string name;
    std::vector<std::string> args;
    const char * doc = nullptr;
};

#if HAVE_BOEHMGC
    typedef std::map<std::string, Value *, std::less<std::string>, traceable_allocator<std::pair<const std::string, Value *> > > ValMap;
#else
    typedef std::map<std::string, Value *> ValMap;
#endif

struct Env
{
    Env * up;
    unsigned short prevWith:14; // nr of levels up to next `with' environment
    enum { Plain = 0, HasWithExpr, HasWithAttrs } type:2;
    Value * values[0];
};

void printEnvBindings(const EvalState &es, const Expr & expr, const Env & env);
void printEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, int lvl = 0);

std::unique_ptr<ValMap> mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env);

void copyContext(const Value & v, PathSet & context);


std::ostream & printValue(const EvalState & state, std::ostream & str, const Value & v);
std::string printValue(const EvalState & state, const Value & v);
std::ostream & operator << (std::ostream & os, const ValueType t);


// FIXME: maybe change this to an std::variant<SourcePath, URL>.
typedef std::pair<std::string, std::string> SearchPathElem;
typedef std::list<SearchPathElem> SearchPath;


/* Initialise the Boehm GC, if applicable. */
void initGC();


struct RegexCache;

std::shared_ptr<RegexCache> makeRegexCache();

struct DebugTrace {
    std::shared_ptr<AbstractPos> pos;
    const Expr & expr;
    const Env & env;
    hintformat hint;
    bool isError;
};

void debugError(Error * e, Env & env, Expr & expr);

class EvalState : public std::enable_shared_from_this<EvalState>
{
public:
    SymbolTable symbols;
    PosTable positions;

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName, sValue,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls,
        sFile, sLine, sColumn, sFunctor, sToString,
        sRight, sWrong, sStructuredAttrs, sBuilder, sArgs,
        sContentAddressed, sImpure,
        sOutputHash, sOutputHashAlgo, sOutputHashMode,
        sRecurseForDerivations,
        sDescription, sSelf, sEpsilon, sStartSet, sOperator, sKey, sPath,
        sPrefix,
        sOutputSpecified;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    RepairFlag repair;

    Bindings emptyBindings;

    const ref<FSInputAccessor> rootFS;
    const ref<MemoryInputAccessor> corepkgsFS;
    const ref<MemoryInputAccessor> internalFS;

    const SourcePath derivationInternal;

    const SourcePath callFlakeInternal;

    /* A map keyed by InputAccessor::number that keeps input accessors
       alive. */
    std::unordered_map<size_t, ref<InputAccessor>> inputAccessors;

    /* Store used to materialise .drv files. */
    const ref<Store> store;

    /* Store used to build stuff. */
    const ref<Store> buildStore;

    RootValue vImportedDrvToDerivation = nullptr;

    /* Debugger */
    void (* debugRepl)(ref<EvalState> es, const ValMap & extraEnv);
    bool debugStop;
    bool debugQuit;
    int trylevel;
    std::list<DebugTrace> debugTraces;
    std::map<const Expr*, const std::shared_ptr<const StaticEnv>> exprEnvs;
    const std::shared_ptr<const StaticEnv> getStaticEnv(const Expr & expr) const
    {
        auto i = exprEnvs.find(&expr);
        if (i != exprEnvs.end())
            return i->second;
        else
            return std::shared_ptr<const StaticEnv>();;
    }

    void runDebugRepl(const Error * error, const Env & env, const Expr & expr);

    template<class E>
    [[gnu::noinline, gnu::noreturn]]
    void debugThrow(E && error, const Env & env, const Expr & expr)
    {
        if (debugRepl)
            runDebugRepl(&error, env, expr);

        throw std::move(error);
    }

    template<class E>
    [[gnu::noinline, gnu::noreturn]]
    void debugThrowLastTrace(E && e)
    {
        // Call this in the situation where Expr and Env are inaccessible.
        // The debugger will start in the last context that's in the
        // DebugTrace stack.
        if (debugRepl && !debugTraces.empty()) {
            const DebugTrace & last = debugTraces.front();
            runDebugRepl(&e, last.env, last.expr);
        }

        throw std::move(e);
    }


private:

    /* Cache for calls to addToStore(); maps source paths to the store
       paths. */
    std::map<SourcePath, StorePath> srcToStore;

    /* A cache from path names to parse trees. */
#if HAVE_BOEHMGC
    typedef std::map<SourcePath, Expr *, std::less<SourcePath>, traceable_allocator<std::pair<const SourcePath, Expr *>>> FileParseCache;
#else
    typedef std::map<SourcePath, Expr *> FileParseCache;
#endif
    FileParseCache fileParseCache;

    /* A cache from path names to values. */
#if HAVE_BOEHMGC
    typedef std::map<SourcePath, Value, std::less<SourcePath>, traceable_allocator<std::pair<const SourcePath, Value>>> FileEvalCache;
#else
    typedef std::map<SourcePath, Value> FileEvalCache;
#endif
    FileEvalCache fileEvalCache;

    SearchPath searchPath;

    std::map<std::string, std::optional<SourcePath>> searchPathResolved;

    /* Cache used by prim_match(). */
    std::shared_ptr<RegexCache> regexCache;

#if HAVE_BOEHMGC
    /* Allocation cache for GC'd Value objects. */
    std::shared_ptr<void *> valueAllocCache;

    /* Allocation cache for size-1 Env objects. */
    std::shared_ptr<void *> env1AllocCache;
#endif

public:

    EvalState(
        const Strings & _searchPath,
        ref<Store> store,
        std::shared_ptr<Store> buildStore = nullptr);
    ~EvalState();

    void addToSearchPath(const std::string & s);

    SearchPath getSearchPath() { return searchPath; }

    SourcePath rootPath(const Path & path);

    void registerAccessor(ref<InputAccessor> accessor);

    /* Convert a path to a string representation of the format
       `/__virtual__/<accessor-number>/<path>`. */
    std::string encodePath(const SourcePath & path);

    /* Decode a path encoded by `encodePath()`. */
    SourcePath decodePath(std::string_view s, PosIdx pos = noPos);

    /* Decode all virtual paths in a string, i.e. all
       /__virtual__/... substrings are replaced by the corresponding
       input accessor. */
    std::string decodePaths(std::string_view s);

    /* Allow access to a path. */
    void allowPath(const Path & path);

    /* Allow access to a store path. Note that this gets remapped to
       the real store path if `store` is a chroot store. */
    void allowPath(const StorePath & storePath);

    /* Allow access to a store path and return it as a string. */
    void allowAndSetStorePathString(const StorePath & storePath, Value & v);

    void checkURI(const std::string & uri);

    /* When using a diverted store and 'path' is in the Nix store, map
       'path' to the diverted location (e.g. /nix/store/foo is mapped
       to /home/alice/my-nix/nix/store/foo). However, this is only
       done if the context is not empty, since otherwise we're
       probably trying to read from the actual /nix/store. This is
       intended to distinguish between import-from-derivation and
       sources stored in the actual /nix/store. */
    Path toRealPath(const Path & path, const PathSet & context);

    /* Parse a Nix expression from the specified file. */
    Expr * parseExprFromFile(const SourcePath & path);
    Expr * parseExprFromFile(const SourcePath & path, std::shared_ptr<StaticEnv> & staticEnv);

    /* Parse a Nix expression from the specified string. */
    Expr * parseExprFromString(std::string s, const SourcePath & basePath, std::shared_ptr<StaticEnv> & staticEnv);
    Expr * parseExprFromString(std::string s, const SourcePath & basePath);

    Expr * parseStdin();

    /* Evaluate an expression read from the given file to normal
       form. Optionally enforce that the top-level expression is
       trivial (i.e. doesn't require arbitrary computation). */
    void evalFile(const SourcePath & path, Value & v, bool mustBeTrivial = false);

    void resetFileCache();

    /* Look up a file in the search path. */
    SourcePath findFile(const std::string_view path);
    SourcePath findFile(SearchPath & searchPath, const std::string_view path, const PosIdx pos = noPos);

    /* If the specified search path element is a URI, download it. */
    std::optional<SourcePath> resolveSearchPathElem(
        const SearchPathElem & elem,
        bool initAccessControl = false);

    /* Evaluate an expression to normal form, storing the result in
       value `v'. */
    void eval(Expr * e, Value & v);

    /* Evaluation the expression, then verify that it has the expected
       type. */
    inline bool evalBool(Env & env, Expr * e);
    inline bool evalBool(Env & env, Expr * e, const PosIdx pos);
    inline void evalAttrs(Env & env, Expr * e, Value & v);

    /* If `v' is a thunk, enter it and overwrite `v' with the result
       of the evaluation of the thunk.  If `v' is a delayed function
       application, call the function and overwrite `v' with the
       result.  Otherwise, this is a no-op. */
    inline void forceValue(Value & v, const PosIdx pos);

    template <typename Callable>
    inline void forceValue(Value & v, Callable getPos);

    /* Force a value, then recursively force list elements and
       attributes. */
    void forceValueDeep(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    NixInt forceInt(Value & v, const PosIdx pos);
    NixFloat forceFloat(Value & v, const PosIdx pos);
    bool forceBool(Value & v, const PosIdx pos);

    void forceAttrs(Value & v, const PosIdx pos);

    template <typename Callable>
    inline void forceAttrs(Value & v, Callable getPos);

    inline void forceList(Value & v, const PosIdx pos);
    void forceFunction(Value & v, const PosIdx pos); // either lambda or primop
    std::string_view forceString(Value & v, const PosIdx pos = noPos);
    std::string_view forceString(Value & v, PathSet & context, const PosIdx pos = noPos);
    std::string_view forceStringNoCtx(Value & v, const PosIdx pos = noPos);

    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const char * s, const std::string & s2);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s, std::string_view s2);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const char * s, const std::string & s2,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s, const std::string & s2,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const char * s, const std::string & s2, const std::string & s3,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s, std::string_view s2) const;
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s, const std::string & s2, const std::string & s3,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const char * s, const std::string & s2, const std::string & s3);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const char * s, const std::string & s2, const std::string & s3);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx pos, const Suggestions & suggestions, const char * s, const std::string & s2,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwEvalError(const PosIdx p1, const char * s, const Symbol sym, const PosIdx p2,
        Env & env, Expr & expr);

    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const char * s, const Value & v);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const char * s, const Value & v,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const char * s);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const char * s,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const char * s, const ExprLambda & fun, const Symbol s2,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const PosIdx pos, const Suggestions & suggestions, const char * s, const ExprLambda & fun, const Symbol s2,
        Env & env, Expr & expr);
    [[gnu::noinline, gnu::noreturn]]
    void throwTypeError(const char * s, const Value & v,
        Env & env, Expr & expr);

    [[gnu::noinline, gnu::noreturn]]
    void throwAssertionError(const PosIdx pos, const char * s, const std::string & s1,
        Env & env, Expr & expr);

    [[gnu::noinline, gnu::noreturn]]
    void throwUndefinedVarError(const PosIdx pos, const char * s, const std::string & s1,
        Env & env, Expr & expr);

    [[gnu::noinline, gnu::noreturn]]
    void throwMissingArgumentError(const PosIdx pos, const char * s, const std::string & s1,
        Env & env, Expr & expr);

    [[gnu::noinline]]
    void addErrorTrace(Error & e, const char * s, const std::string & s2) const;
    [[gnu::noinline]]
    void addErrorTrace(Error & e, const PosIdx pos, const char * s, const std::string & s2) const;

public:
    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

    std::optional<std::string> tryAttrsToString(const PosIdx pos, Value & v,
        PathSet & context, bool coerceMore = false, bool copyToStore = true);

    /* String coercion.  Converts strings, paths and derivations to a
       string.  If `coerceMore' is set, also converts nulls, integers,
       booleans and lists to a string.  If `copyToStore' is set,
       referenced paths are copied to the Nix store as a side effect. */
    BackedStringView coerceToString(const PosIdx pos, Value & v, PathSet & context,
        bool coerceMore = false, bool copyToStore = true);

    StorePath copyPathToStore(PathSet & context, const SourcePath & path);

    /* Path coercion.  Converts strings, paths and derivations to a
       path.  The result is guaranteed to be a canonicalised, absolute
       path.  Nothing is copied to the store. */
    SourcePath coerceToPath(const PosIdx pos, Value & v, PathSet & context);

    /* Like coerceToPath, but the result must be a store path. */
    StorePath coerceToStorePath(const PosIdx pos, Value & v, PathSet & context);

public:

    /* The base environment, containing the builtin functions and
       values. */
    Env & baseEnv;

    /* The same, but used during parsing to resolve variables. */
    std::shared_ptr<StaticEnv> staticBaseEnv; // !!! should be private

private:

    unsigned int baseEnvDispl = 0;

    void createBaseEnv();

    Value * addConstant(const std::string & name, Value & v);

    void addConstant(const std::string & name, Value * v);

    Value * addPrimOp(const std::string & name,
        size_t arity, PrimOpFun primOp);

    Value * addPrimOp(PrimOp && primOp);

public:

    Value & getBuiltin(const std::string & name);

    struct Doc
    {
        Pos pos;
        std::optional<std::string> name;
        size_t arity;
        std::vector<std::string> args;
        const char * doc;
    };

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
        std::shared_ptr<StaticEnv> & staticEnv);

public:

    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    bool isFunctor(Value & fun);

    // FIXME: use std::span
    void callFunction(Value & fun, size_t nrArgs, Value * * args, Value & vRes, const PosIdx pos);

    void callFunction(Value & fun, Value & arg, Value & vRes, const PosIdx pos)
    {
        Value * args[] = {&arg};
        callFunction(fun, 1, args, vRes, pos);
    }

    /* Automatically call a function for which each argument has a
       default value or has a binding in the `args' map. */
    void autoCallFunction(Bindings & args, Value & fun, Value & res);

    /* Allocation primitives. */
    inline Value * allocValue();
    inline Env & allocEnv(size_t size);

    Value * allocAttr(Value & vAttrs, Symbol name);
    Value * allocAttr(Value & vAttrs, std::string_view name);

    Bindings * allocBindings(size_t capacity);

    BindingsBuilder buildBindings(size_t capacity)
    {
        return BindingsBuilder(*this, allocBindings(capacity));
    }

    void mkList(Value & v, size_t length);
    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, PosIdx pos);

    void concatLists(Value & v, size_t nrLists, Value * * lists, const PosIdx pos);

    /* Print statistics. */
    void printStats();

    /* Realise the given context, and return a mapping from the placeholders
     * used to construct the associated value to their final store path
     */
    [[nodiscard]] StringMap realiseContext(const PathSet & context);

private:

    unsigned long nrEnvs = 0;
    unsigned long nrValuesInEnvs = 0;
    unsigned long nrValues = 0;
    unsigned long nrListElems = 0;
    unsigned long nrLookups = 0;
    unsigned long nrAttrsets = 0;
    unsigned long nrAttrsInAttrsets = 0;
    unsigned long nrAvoided = 0;
    unsigned long nrOpUpdates = 0;
    unsigned long nrOpUpdateValuesCopied = 0;
    unsigned long nrListConcats = 0;
    unsigned long nrPrimOpCalls = 0;
    unsigned long nrFunctionCalls = 0;

    bool countCalls;

    typedef std::map<std::string, size_t> PrimOpCalls;
    PrimOpCalls primOpCalls;

    typedef std::map<ExprLambda *, size_t> FunctionCalls;
    FunctionCalls functionCalls;

    void incrFunctionCall(ExprLambda * fun);

    typedef std::map<PosIdx, size_t> AttrSelects;
    AttrSelects attrSelects;

    friend struct ExprOpUpdate;
    friend struct ExprOpConcatLists;
    friend struct ExprVar;
    friend struct ExprString;
    friend struct ExprInt;
    friend struct ExprFloat;
    friend struct ExprPath;
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const PosIdx pos, Value * * args, Value & v);
    friend void prim_match(EvalState & state, const PosIdx pos, Value * * args, Value & v);
    friend void prim_split(EvalState & state, const PosIdx pos, Value * * args, Value & v);

    friend struct Value;
};

struct DebugTraceStacker {
    DebugTraceStacker(EvalState & evalState, DebugTrace t);
    ~DebugTraceStacker()
    {
        // assert(evalState.debugTraces.front() == trace);
        evalState.debugTraces.pop_front();
    }
    EvalState & evalState;
    DebugTrace trace;
};

/* Return a string representing the type of the value `v'. */
std::string_view showType(ValueType type);
std::string showType(const Value & v);

/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
NixStringContextElem decodeContext(const Store & store, std::string_view s);

/* If `path' refers to a directory, then append "/default.nix". */
SourcePath resolveExprPath(const SourcePath & path);

struct InvalidPathError : EvalError
{
    Path path;
    InvalidPathError(const Path & path);
#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~InvalidPathError() throw () { };
#endif
};

struct EvalSettings : Config
{
    EvalSettings();

    static Strings getDefaultNixPath();

    static bool isPseudoUrl(std::string_view s);

    static std::string resolvePseudoUrl(std::string_view url);

    Setting<bool> enableNativeCode{this, false, "allow-unsafe-native-code-during-evaluation",
        "Whether builtin functions that allow executing native code should be enabled."};

    Setting<Strings> nixPath{
        this, getDefaultNixPath(), "nix-path",
        "List of directories to be searched for `<...>` file references."};

    Setting<bool> restrictEval{
        this, false, "restrict-eval",
        R"(
          If set to `true`, the Nix evaluator will not allow access to any
          files outside of the Nix search path (as set via the `NIX_PATH`
          environment variable or the `-I` option), or to URIs outside of
          `allowed-uri`. The default is `false`.
        )"};

    Setting<bool> pureEval{this, false, "pure-eval",
        "Whether to restrict file system and network access to files specified by cryptographic hash."};

    Setting<bool> enableImportFromDerivation{
        this, true, "allow-import-from-derivation",
        R"(
          By default, Nix allows you to `import` from a derivation, allowing
          building at evaluation time. With this option set to false, Nix will
          throw an error when evaluating an expression that uses this feature,
          allowing users to ensure their evaluation will not require any
          builds to take place.
        )"};

    Setting<Strings> allowedUris{this, {}, "allowed-uris",
        R"(
          A list of URI prefixes to which access is allowed in restricted
          evaluation mode. For example, when set to
          `https://github.com/NixOS`, builtin functions such as `fetchGit` are
          allowed to access `https://github.com/NixOS/patchelf.git`.
        )"};

    Setting<bool> traceFunctionCalls{this, false, "trace-function-calls",
        R"(
          If set to `true`, the Nix evaluator will trace every function call.
          Nix will print a log message at the "vomit" level for every function
          entrance and function exit.

              function-trace entered undefined position at 1565795816999559622
              function-trace exited undefined position at 1565795816999581277
              function-trace entered /nix/store/.../example.nix:226:41 at 1565795253249935150
              function-trace exited /nix/store/.../example.nix:226:41 at 1565795253249941684

          The `undefined position` means the function call is a builtin.

          Use the `contrib/stack-collapse.py` script distributed with the Nix
          source code to convert the trace logs in to a format suitable for
          `flamegraph.pl`.
        )"};

    Setting<bool> useEvalCache{this, true, "eval-cache",
        "Whether to use the flake evaluation cache."};

    Setting<bool> ignoreExceptionsDuringTry{this, false, "ignore-try",
        R"(
          If set to true, ignore exceptions inside 'tryEval' calls when evaluating nix expressions in
          debug mode (using the --debugger flag). By default the debugger will pause on all exceptions.
        )"};

    Setting<bool> traceVerbose{this, false, "trace-verbose",
        "Whether `builtins.traceVerbose` should trace its first argument when evaluated."};
};

extern EvalSettings evalSettings;

}

#include "eval-inline.hh"
