#pragma once

#include "attr-set.hh"
#include "value.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "hash.hh"
#include "config.hh"
#include "gc.hh"

#include <map>
#include <optional>
#include <unordered_map>


namespace nix {


class Store;
class EvalState;
struct Derivation; // FIXME: remove
struct StorePath;
enum RepairFlag : bool;


typedef void (* PrimOpFun) (EvalState & state, const Pos & pos, Value * * args, Value & v);


struct PrimOp
{
    PrimOpFun fun;
    size_t arity;
    Symbol name;
    PrimOp(PrimOpFun fun, size_t arity, Symbol name)
        : fun(fun), arity(arity), name(name) { }
};


struct Env : Object
{
    Env * up;
    Value * values[0];

private:

    constexpr static size_t maxSize = 1 << 16;
    constexpr static size_t maxPrevWith = 1 << 10;

    Env(Tag type, size_t size, size_t prevWith)
        : Object(type, size | (prevWith << 16))
    {
        if (size >= maxSize)
            throw Error("environment size %d is too big", size);

        if (prevWith >= maxPrevWith)
            throw Error("too many nesting levels");

        for (size_t i = 0; i < size; i++)
            values[i] = nullptr;
    }

    friend class GC;

public:

    unsigned short getPrevWith() const
    {
        return getMisc() >> 16;
    }

    unsigned short getSize() const
    {
        return getMisc() & 0xffff;
    }

    size_t words() const
    {
        return wordsFor(getSize());
    }

    static size_t wordsFor(unsigned short size)
    {
        return 2 + size;
    }
};


Value & mkString(Value & v, std::string_view s, const PathSet & context);


/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, StorePath> SrcToStore;


std::ostream & operator << (std::ostream & str, const Value & v);


typedef std::pair<std::string, std::string> SearchPathElem;
typedef std::list<SearchPathElem> SearchPath;


class EvalState
{
public:
    SymbolTable & symbols{nix::symbols}; // FIXME: remove

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName, sValue,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls,
        sFile, sLine, sColumn, sFunctor, sToString,
        sRight, sWrong, sStructuredAttrs, sBuilder, sArgs,
        sOutputHash, sOutputHashAlgo, sOutputHashMode;
    Symbol sDerivationNix;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    RepairFlag repair;

    /* The allowed filesystem paths in restricted or pure evaluation
       mode. */
    std::optional<PathSet> allowedPaths;

    Ptr<Bindings> emptyBindings;

    const ref<Store> store;

private:
    SrcToStore srcToStore;

    /* A cache from path names to parse trees. */
    typedef std::map<Path, Expr *> FileParseCache;
    FileParseCache fileParseCache;

    /* A cache from path names to values. */
    typedef std::map<Path, Ptr<Value>> FileEvalCache;
    FileEvalCache fileEvalCache;

    SearchPath searchPath;

    std::map<std::string, std::pair<bool, std::string>> searchPathResolved;

    /* Cache used by checkSourcePath(). */
    std::unordered_map<Path, Path> resolvedPaths;

public:

    EvalState(const Strings & _searchPath, ref<Store> store);
    ~EvalState();

    void addToSearchPath(const string & s);

    SearchPath getSearchPath() { return searchPath; }

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
    Expr * parseExprFromString(const string & s, const Path & basePath, StaticEnv & staticEnv);
    Expr * parseExprFromString(const string & s, const Path & basePath);

    Expr * parseStdin();

    /* Evaluate an expression read from the given file to normal
       form. */
    void evalFile(const Path & path, Value & v);

    void resetFileCache();

    /* Look up a file in the search path. */
    Path findFile(const string & path);
    Path findFile(SearchPath & searchPath, const string & path, const Pos & pos = noPos);

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
    inline void forceValue(Value & v, const Pos & pos = noPos);

    /* Force a value, then recursively force list elements and
       attributes. */
    void forceValueDeep(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    NixInt forceInt(Value & v, const Pos & pos);
    NixFloat forceFloat(Value & v, const Pos & pos);
    bool forceBool(Value & v, const Pos & pos);
    inline void forceAttrs(Value & v);
    inline void forceAttrs(Value & v, const Pos & pos);
    inline void forceList(Value & v);
    inline void forceList(Value & v, const Pos & pos);
    void forceFunction(Value & v, const Pos & pos); // either lambda or primop
    string forceString(Value & v, const Pos & pos = noPos);
    string forceString(Value & v, PathSet & context, const Pos & pos = noPos);
    string forceStringNoCtx(Value & v, const Pos & pos = noPos);

    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

    std::optional<string> tryAttrsToString(const Pos & pos, Value & v,
        PathSet & context, bool coerceMore = false, bool copyToStore = true);

    /* String coercion.  Converts strings, paths and derivations to a
       string.  If `coerceMore' is set, also converts nulls, integers,
       booleans and lists to a string.  If `copyToStore' is set,
       referenced paths are copied to the Nix store as a side effect. */
    string coerceToString(const Pos & pos, Value & v, PathSet & context,
        bool coerceMore = false, bool copyToStore = true);

    string copyPathToStore(PathSet & context, const Path & path);

    /* Path coercion.  Converts strings, paths and derivations to a
       path.  The result is guaranteed to be a canonicalised, absolute
       path.  Nothing is copied to the store. */
    Path coerceToPath(const Pos & pos, Value & v, PathSet & context);

public:

    /* The base environment, containing the builtin functions and
       values. */
    Ptr<Env> baseEnv;

    /* The same, but used during parsing to resolve variables. */
    StaticEnv staticBaseEnv; // !!! should be private

private:

    unsigned int baseEnvDispl = 0;

    void createBaseEnv();

    Value * addConstant(const string & name, Value & v);

    Value * addPrimOp(const string & name,
        size_t arity, PrimOpFun primOp);

public:

    Value & getBuiltin(const string & name);

private:

    inline Value * lookupVar(Env * env, const ExprVar & var, bool noEval);

    friend struct ExprVar;
    friend struct ExprAttrs;
    friend struct ExprLet;

    Expr * parse(const char * text, const Path & path,
        const Path & basePath, StaticEnv & staticEnv);

public:

    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    bool isFunctor(Value & fun);

    void callFunction(Value & fun, Value & arg, Value & v, const Pos & pos);
    void callPrimOp(Value & fun, Value & arg, Value & v, const Pos & pos);

    /* Automatically call a function for which each argument has a
       default value or has a binding in the `args' map. */
    void autoCallFunction(Bindings & args, Value & fun, Value & res);

    /* Allocation primitives. */
    Ptr<Value> allocValue()
    {
        nrValues++;
        return gc.alloc<Value>(Value::words());
    }

    Ptr<Env> allocEnv(size_t size, size_t prevWith = 0, Tag type = tEnv);

    // Note: the resulting Value is only reachable as long as vAttrs
    // is reachable.
    Value * allocAttr(Value & vAttrs, const Symbol & name);

    void mkList(Value & v, size_t length);
    void mkAttrs(Value & v, size_t capacity);
    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, Pos * pos);

    void concatLists(Value & v, size_t nrLists, Value * * lists, const Pos & pos);

    /* Print statistics. */
    void printStats();

    void realiseContext(const PathSet & context);

private:

    unsigned long nrEnvs = 0;
    unsigned long nrValuesInEnvs = 0;
    unsigned long nrValues = 0;
    unsigned long nrListElems = 0;
    unsigned long nrAttrsets = 0;
    unsigned long nrAttrsInAttrsets = 0;
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
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v);

public:

    std::function<void(const StorePath & drvPath, const Derivation & drv)> derivationHook;
};


/* Return a string representing the type of the value `v'. */
string showType(const Value & v);

/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
std::pair<string, string> decodeContext(const string & s);

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

    Setting<Strings> nixPath{this, getDefaultNixPath(), "nix-path",
        "List of directories to be searched for <...> file references."};

    Setting<bool> restrictEval{this, false, "restrict-eval",
        "Whether to restrict file system access to paths in $NIX_PATH, "
        "and network access to the URI prefixes listed in 'allowed-uris'."};

    Setting<bool> pureEval{this, false, "pure-eval",
        "Whether to restrict file system and network access to files specified by cryptographic hash."};

    Setting<bool> enableImportFromDerivation{this, true, "allow-import-from-derivation",
        "Whether the evaluator allows importing the result of a derivation."};

    Setting<Strings> allowedUris{this, {}, "allowed-uris",
        "Prefixes of URIs that builtin functions such as fetchurl and fetchGit are allowed to fetch."};

    Setting<bool> traceFunctionCalls{this, false, "trace-function-calls",
        "Emit log messages for each function entry and exit at the 'vomit' log level (-vvvv)"};
};

extern EvalSettings evalSettings;

}
