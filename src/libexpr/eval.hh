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


/* Attribute sets are represented as a vector of attributes, sorted by
   symbol (i.e. pointer to the attribute name in the symbol table). */
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


typedef void (* PrimOpFun) (EvalState & state, Value * * args, Value & v);


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
    unsigned int prevWith; // nr of levels up to next `with' environment
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

struct EvalState;


std::ostream & operator << (std::ostream & str, const Value & v);


class EvalState 
{
public:
    SymbolTable symbols;

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    bool repair;

private:
    SrcToStore srcToStore; 

    /* A cache from path names to parse trees. */
    std::map<Path, Expr *> parseTrees;

    /* A cache from path names to values. */
#if HAVE_BOEHMGC
    typedef std::map<Path, Value, std::less<Path>, gc_allocator<std::pair<const Path, Value> > > FileEvalCache;
#else
    typedef std::map<Path, Value> FileEvalCache;
#endif
    FileEvalCache fileEvalCache;

    typedef list<std::pair<string, Path> > SearchPath;
    SearchPath searchPath;
    SearchPath::iterator searchPathInsertionPoint;

public:
    
    EvalState();
    ~EvalState();

    void addToSearchPath(const string & s);

    /* Parse a Nix expression from the specified file.  If `path'
       refers to a directory, then "/default.nix" is appended. */
    Expr * parseExprFromFile(Path path);

    /* Parse a Nix expression from the specified string. */
    Expr * parseExprFromString(const string & s, const Path & basePath);
    
    /* Evaluate an expression read from the given file to normal
       form. */
    void evalFile(const Path & path, Value & v);

    /* Look up a file in the search path. */
    Path findFile(const string & path);

    /* Evaluate an expression to normal form, storing the result in
       value `v'. */
    void eval(Expr * e, Value & v);

    /* Evaluation the expression, then verify that it has the expected
       type. */
    inline bool evalBool(Env & env, Expr * e);
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
    int forceInt(Value & v);
    bool forceBool(Value & v);
    inline void forceAttrs(Value & v);
    inline void forceList(Value & v);
    void forceFunction(Value & v); // either lambda or primop
    string forceString(Value & v);
    string forceString(Value & v, PathSet & context);
    string forceStringNoCtx(Value & v);

    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

    /* String coercion.  Converts strings, paths and derivations to a
       string.  If `coerceMore' is set, also converts nulls, integers,
       booleans and lists to a string.  If `copyToStore' is set,
       referenced paths are copied to the Nix store as a side effect.q */
    string coerceToString(Value & v, PathSet & context,
        bool coerceMore = false, bool copyToStore = true);

    /* Path coercion.  Converts strings, paths and derivations to a
       path.  The result is guaranteed to be a canonicalised, absolute
       path.  Nothing is copied to the store. */
    Path coerceToPath(Value & v, PathSet & context);

private:

    /* The base environment, containing the builtin functions and
       values. */
    Env & baseEnv;

    unsigned int baseEnvDispl;

public:
    
    /* The same, but used during parsing to resolve variables. */
    StaticEnv staticBaseEnv; // !!! should be private

private:
    
    void createBaseEnv();
    
    void addConstant(const string & name, Value & v);

    void addPrimOp(const string & name,
        unsigned int arity, PrimOpFun primOp);

    string showTypeOrXml(Value &v);

    inline Value * lookupVar(Env * env, const VarRef & var);
    
    friend class ExprVar;
    friend class ExprAttrs;
    friend class ExprLet;

    Expr * parse(const char * text,
        const Path & path, const Path & basePath);

public:
    
    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    void callFunction(Value & fun, Value & arg, Value & v);

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

    void concatLists(Value & v, unsigned int nrLists, Value * * lists);

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

    typedef std::map<Pos, unsigned int> FunctionCalls;
    FunctionCalls functionCalls;

    typedef std::map<Pos, unsigned int> AttrSelects;
    AttrSelects attrSelects;

    friend class ExprOpUpdate;
    friend class ExprOpConcatLists;
    friend class ExprSelect;
    friend void prim_getAttr(EvalState & state, Value * * args, Value & v);
};


/* Return a string representing the type of the value `v'. */
string showType(const Value & v);


}
