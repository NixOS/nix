#pragma once

#include "value.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "hash.hh"

#include <map>

#if HAVE_BOEHMGC
#include <gc/gc_allocator.h>
#endif


namespace nix {


class EvalState;
struct Attr;


/* Sets are represented as a vector of attributes, sorted by symbol
   (i.e. pointer to the attribute name in the symbol table). */
#if HAVE_BOEHMGC
typedef std::vector<Attr, gc_allocator<Attr> > BindingsBase;
#else
typedef std::vector<Attr> BindingsBase;
#endif


class Bindings : public BindingsBase
{
public:
    iterator find(const Symbol & name);
    void sort();
};


typedef void (* PrimOpFun) (EvalState & state, const Pos & pos, Value * * args, Value & v);


struct PrimOp
{
    PrimOpFun fun;
    unsigned int arity;
    Symbol name;
    PrimOp(PrimOpFun fun, unsigned int arity, Symbol name)
        : fun(fun), arity(arity), name(name) { }
};


struct Env
{
    Env * up;
    unsigned short prevWith; // nr of levels up to next `with' environment
    bool haveWithAttrs;
    Value * values[0];
};


struct Attr
{
    Symbol name;
    Value * value;
    Pos * pos;
    Attr(Symbol name, Value * value, Pos * pos = &noPos)
        : name(name), value(value), pos(pos) { };
    Attr() : pos(&noPos) { };
    bool operator < (const Attr & a) const
    {
        return name < a.name;
    }
};


void mkString(Value & v, const string & s, const PathSet & context = PathSet());

void copyContext(const Value & v, PathSet & context);


/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, Path> SrcToStore;


std::ostream & operator << (std::ostream & str, const Value & v);


class EvalState
{
public:
    SymbolTable symbols;

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName, sValue,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls,
        sFile, sLine, sColumn;
    Symbol sDerivationNix;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    bool repair;

private:
    SrcToStore srcToStore;

    /* A cache from path names to values. */
#if HAVE_BOEHMGC
    typedef std::map<Path, Value, std::less<Path>, gc_allocator<std::pair<const Path, Value> > > FileEvalCache;
#else
    typedef std::map<Path, Value> FileEvalCache;
#endif
    FileEvalCache fileEvalCache;

    typedef list<std::pair<string, Path> > SearchPath;
    SearchPath searchPath;

public:

    EvalState(const Strings & _searchPath);
    ~EvalState();

    void addToSearchPath(const string & s, bool warn = false);

    /* Parse a Nix expression from the specified file. */
    Expr * parseExprFromFile(const Path & path);
    Expr * parseExprFromFile(const Path & path, StaticEnv & staticEnv);

    /* Parse a Nix expression from the specified string. */
    Expr * parseExprFromString(const string & s, const Path & basePath, StaticEnv & staticEnv);
    Expr * parseExprFromString(const string & s, const Path & basePath);

    /* Evaluate an expression read from the given file to normal
       form. */
    void evalFile(const Path & path, Value & v);

    void resetFileCache();

    /* Look up a file in the search path. */
    Path findFile(const string & path);

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
    inline void forceValue(Value & v);

    /* Force a value, then recursively force list elements and
       attributes. */
    void strictForceValue(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    NixInt forceInt(Value & v, const Pos & pos);
    bool forceBool(Value & v);
    inline void forceAttrs(Value & v);
    inline void forceAttrs(Value & v, const Pos & pos);
    inline void forceList(Value & v);
    inline void forceList(Value & v, const Pos & pos);
    void forceFunction(Value & v, const Pos & pos); // either lambda or primop
    string forceString(Value & v, const Pos & pos = noPos);
    string forceString(Value & v, PathSet & context);
    string forceStringNoCtx(Value & v, const Pos & pos = noPos);

    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

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
    Env & baseEnv;

    /* The same, but used during parsing to resolve variables. */
    StaticEnv staticBaseEnv; // !!! should be private

private:

    unsigned int baseEnvDispl;

    void createBaseEnv();

    void addConstant(const string & name, Value & v);

    void addPrimOp(const string & name,
        unsigned int arity, PrimOpFun primOp);

public:

    void getBuiltin(const string & name, Value & v);

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

    void callFunction(Value & fun, Value & arg, Value & v, const Pos & pos);
    void callPrimOp(Value & fun, Value & arg, Value & v, const Pos & pos);

    /* Automatically call a function for which each argument has a
       default value or has a binding in the `args' map. */
    void autoCallFunction(Bindings & args, Value & fun, Value & res);

    /* Allocation primitives. */
    Value * allocValue();
    Env & allocEnv(unsigned int size);

    Value * allocAttr(Value & vAttrs, const Symbol & name);

    void mkList(Value & v, unsigned int length);
    void mkAttrs(Value & v, unsigned int expected);
    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, Pos * pos);

    void concatLists(Value & v, unsigned int nrLists, Value * * lists, const Pos & pos);

    /* Print statistics. */
    void printStats();

private:

    unsigned long nrEnvs;
    unsigned long nrValuesInEnvs;
    unsigned long nrValues;
    unsigned long nrListElems;
    unsigned long nrAttrsets;
    unsigned long nrOpUpdates;
    unsigned long nrOpUpdateValuesCopied;
    unsigned long nrListConcats;
    unsigned long nrPrimOpCalls;
    unsigned long nrFunctionCalls;

    bool countCalls;

    typedef std::map<Symbol, unsigned int> PrimOpCalls;
    PrimOpCalls primOpCalls;

    typedef std::map<ExprLambda *, unsigned int> FunctionCalls;
    FunctionCalls functionCalls;

    void incrFunctionCall(ExprLambda * fun);

    typedef std::map<Pos, unsigned int> AttrSelects;
    AttrSelects attrSelects;

    friend struct ExprOpUpdate;
    friend struct ExprOpConcatLists;
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v);
};


/* Return a string representing the type of the value `v'. */
string showType(const Value & v);


/* If `path' refers to a directory, then append "/default.nix". */
Path resolveExprPath(Path path);


}
