#pragma once

#include "attr-set.hh"
#include "types.hh"
#include "value.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "config.hh"
#include "experimental-features.hh"

#include <map>
#include <optional>
#include <unordered_map>
#include <mutex>


namespace nix {


class Store;
class EvalState;
class StorePath;
enum RepairFlag : bool;


typedef void (* PrimOpFun) (EvalState & state, const Pos & pos, Value * * args, Value & v);


struct PrimOp
{
    PrimOpFun fun;
    size_t arity;
    Symbol name;
    std::vector<std::string> args;
    const char * doc = nullptr;
};


struct Env
{
    Env * up;
    unsigned short prevWith:14; // nr of levels up to next `with' environment
    enum { Plain = 0, HasWithExpr, HasWithAttrs } type:2;
    Value * values[0];
};


void copyContext(const Value & v, PathSet & context);


/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, StorePath> SrcToStore;


std::ostream & operator << (std::ostream & str, const Value & v);


typedef std::pair<std::string, std::string> SearchPathElem;
typedef std::list<SearchPathElem> SearchPath;


/* Initialise the Boehm GC, if applicable. */
void initGC();


struct RegexCache;

std::shared_ptr<RegexCache> makeRegexCache();


class EvalState
{
public:
    SymbolTable symbols;

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName, sValue,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls,
        sFile, sLine, sColumn, sFunctor, sToString,
        sRight, sWrong, sStructuredAttrs, sBuilder, sArgs,
        sContentAddressed, sImpure,
        sOutputHash, sOutputHashAlgo, sOutputHashMode,
        sRecurseForDerivations,
        sDescription, sSelf, sEpsilon, sStartSet, sOperator, sKey, sPath,
        sPrefix;
    Symbol sDerivationNix;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    RepairFlag repair;

    /* The allowed filesystem paths in restricted or pure evaluation
       mode. */
    std::optional<PathSet> allowedPaths;

    Bindings emptyBindings;

    /* Store used to materialise .drv files. */
    const ref<Store> store;

    /* Store used to build stuff. */
    const ref<Store> buildStore;

    RootValue vCallFlake = nullptr;
    RootValue vImportedDrvToDerivation = nullptr;

private:
    SrcToStore srcToStore;

    /* A cache from path names to parse trees. */
#if HAVE_BOEHMGC
    typedef std::map<Path, Expr *, std::less<Path>, traceable_allocator<std::pair<const Path, Expr *> > > FileParseCache;
#else
    typedef std::map<Path, Expr *> FileParseCache;
#endif
    FileParseCache fileParseCache;

    /* A cache from path names to values. */
#if HAVE_BOEHMGC
    typedef std::map<Path, Value, std::less<Path>, traceable_allocator<std::pair<const Path, Value> > > FileEvalCache;
#else
    typedef std::map<Path, Value> FileEvalCache;
#endif
    FileEvalCache fileEvalCache;

    SearchPath searchPath;

    std::map<std::string, std::pair<bool, std::string>> searchPathResolved;

    /* Cache used by checkSourcePath(). */
    std::unordered_map<Path, Path> resolvedPaths;

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

    /* Allow access to a path. */
    void allowPath(const Path & path);

    /* Allow access to a store path. Note that this gets remapped to
       the real store path if `store` is a chroot store. */
    void allowPath(const StorePath & storePath);

    /* Allow access to a store path and return it as a string. */
    void allowAndSetStorePathString(const StorePath & storePath, Value & v);

    /* Check whether access to a path is allowed and throw an error if
       not. Otherwise return the canonicalised path. */
    Path checkSourcePath(const Path & path);

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
    Expr * parseExprFromFile(const Path & path);
    Expr * parseExprFromFile(const Path & path, StaticEnv & staticEnv);

    /* Parse a Nix expression from the specified string. */
    Expr * parseExprFromString(std::string s, const Path & basePath, StaticEnv & staticEnv);
    Expr * parseExprFromString(std::string s, const Path & basePath);

    Expr * parseStdin();

    /* Evaluate an expression read from the given file to normal
       form. Optionally enforce that the top-level expression is
       trivial (i.e. doesn't require arbitrary computation). */
    void evalFile(const Path & path, Value & v, bool mustBeTrivial = false);

    /* Like `cacheFile`, but with an already parsed expression. */
    void cacheFile(
        const Path & path,
        const Path & resolvedPath,
        Expr * e,
        Value & v,
        bool mustBeTrivial = false);

    void resetFileCache();

    /* Look up a file in the search path. */
    Path findFile(const std::string_view path);
    Path findFile(SearchPath & searchPath, const std::string_view path, const Pos & pos = noPos);

    /* If the specified search path element is a URI, download it. */
    std::pair<bool, std::string> resolveSearchPathElem(const SearchPathElem & elem);

    /* Evaluate an expression to normal form, storing the result in
       value `v'. */
    void eval(Expr * e, Value & v);

    /* Evaluation the expression, then verify that it has the expected
       type. */
    inline bool evalBool(Env & env, Expr * e);
    inline bool evalBool(Env & env, Expr * e, const Pos & pos);
    inline void evalAttrs(Env & env, Expr * e, Value & v);

    /* If `v' is a thunk, enter it and overwrite `v' with the result
       of the evaluation of the thunk.  If `v' is a delayed function
       application, call the function and overwrite `v' with the
       result.  Otherwise, this is a no-op. */
    inline void forceValue(Value & v, const Pos & pos);

    template <typename Callable>
    inline void forceValue(Value & v, Callable getPos);

    /* Force a value, then recursively force list elements and
       attributes. */
    void forceValueDeep(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    NixInt forceInt(Value & v, const Pos & pos);
    NixFloat forceFloat(Value & v, const Pos & pos);
    bool forceBool(Value & v, const Pos & pos);

    void forceAttrs(Value & v, const Pos & pos);

    template <typename Callable>
    inline void forceAttrs(Value & v, Callable getPos);

    inline void forceList(Value & v, const Pos & pos);
    void forceFunction(Value & v, const Pos & pos); // either lambda or primop
    std::string_view forceString(Value & v, const Pos & pos = noPos);
    std::string_view forceString(Value & v, PathSet & context, const Pos & pos = noPos);
    std::string_view forceStringNoCtx(Value & v, const Pos & pos = noPos);

    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

    std::optional<std::string> tryAttrsToString(const Pos & pos, Value & v,
        PathSet & context, bool coerceMore = false, bool copyToStore = true);

    /* String coercion.  Converts strings, paths and derivations to a
       string.  If `coerceMore' is set, also converts nulls, integers,
       booleans and lists to a string.  If `copyToStore' is set,
       referenced paths are copied to the Nix store as a side effect. */
    BackedStringView coerceToString(const Pos & pos, Value & v, PathSet & context,
        bool coerceMore = false, bool copyToStore = true,
        bool canonicalizePath = true);

    std::string copyPathToStore(PathSet & context, const Path & path);

    /* Path coercion.  Converts strings, paths and derivations to a
       path.  The result is guaranteed to be a canonicalised, absolute
       path.  Nothing is copied to the store. */
    Path coerceToPath(const Pos & pos, Value & v, PathSet & context);

    /* Like coerceToPath, but the result must be a store path. */
    StorePath coerceToStorePath(const Pos & pos, Value & v, PathSet & context);

public:

    /* The base environment, containing the builtin functions and
       values. */
    Env & baseEnv;

    /* The same, but used during parsing to resolve variables. */
    StaticEnv staticBaseEnv; // !!! should be private

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
        std::optional<Symbol> name;
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

    Expr * parse(char * text, size_t length, FileOrigin origin, const PathView path,
        const PathView basePath, StaticEnv & staticEnv);

public:

    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    bool isFunctor(Value & fun);

    // FIXME: use std::span
    void callFunction(Value & fun, size_t nrArgs, Value * * args, Value & vRes, const Pos & pos);

    void callFunction(Value & fun, Value & arg, Value & vRes, const Pos & pos)
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

    Value * allocAttr(Value & vAttrs, const Symbol & name);
    Value * allocAttr(Value & vAttrs, std::string_view name);

    Bindings * allocBindings(size_t capacity);

    BindingsBuilder buildBindings(size_t capacity)
    {
        return BindingsBuilder(*this, allocBindings(capacity));
    }

    void mkList(Value & v, size_t length);
    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, ptr<Pos> pos);

    void concatLists(Value & v, size_t nrLists, Value * * lists, const Pos & pos);

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

    typedef std::map<Symbol, size_t> PrimOpCalls;
    PrimOpCalls primOpCalls;

    typedef std::map<ExprLambda *, size_t> FunctionCalls;
    FunctionCalls functionCalls;

    void incrFunctionCall(ExprLambda * fun);

    typedef std::map<Pos, size_t> AttrSelects;
    AttrSelects attrSelects;

    friend struct ExprOpUpdate;
    friend struct ExprOpConcatLists;
    friend struct ExprVar;
    friend struct ExprString;
    friend struct ExprInt;
    friend struct ExprFloat;
    friend struct ExprPath;
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v);
    friend void prim_match(EvalState & state, const Pos & pos, Value * * args, Value & v);
    friend void prim_split(EvalState & state, const Pos & pos, Value * * args, Value & v);

    friend struct Value;
};


/* Return a string representing the type of the value `v'. */
std::string_view showType(ValueType type);
std::string showType(const Value & v);

/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
NixStringContextElem decodeContext(const Store & store, std::string_view s);

/* If `path' refers to a directory, then append "/default.nix". */
Path resolveExprPath(Path path);

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
};

extern EvalSettings evalSettings;

static const std::string corepkgsPrefix{"/__corepkgs__/"};

}

#include "eval-inline.hh"
