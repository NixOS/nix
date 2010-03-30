#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "aterm.hh"
#include "nixexpr.hh"


namespace nix {


class Hash;
class EvalState;
struct Env;
struct Value;

typedef ATerm Sym;

typedef std::map<Sym, Value> Bindings;


struct Env
{
    Env * up;
    Bindings bindings;
};


typedef enum {
    tInt = 1,
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList,
    tThunk,
    tApp,
    tLambda,
    tCopy,
    tBlackhole,
    tPrimOp,
    tPrimOpApp,
} ValueType;


typedef void (* PrimOp) (EvalState & state, Value * * args, Value & v);


struct Value
{
    ValueType type;
    union 
    {
        int integer;
        bool boolean;
        struct {
            const char * s;
            const char * * context;
        } string;
        const char * path;
        Bindings * attrs;
        struct {
            unsigned int length;
            Value * elems;
        } list;
        struct {
            Env * env;
            Expr expr;
        } thunk;
        struct {
            Value * left, * right;
        } app;
        struct {
            Env * env;
            Pattern pat;
            Expr body;
        } lambda;
        Value * val;
        struct {
            PrimOp fun;
            unsigned int arity;
        } primOp;
        struct {
            Value * left, * right;
            unsigned int argsLeft;
        } primOpApp;
    };
};


static inline void mkInt(Value & v, int n)
{
    v.type = tInt;
    v.integer = n;
}


static inline void mkBool(Value & v, bool b)
{
    v.type = tBool;
    v.boolean = b;
}


static inline void mkString(Value & v, const char * s)
{
    v.type = tString;
    v.string.s = s;
    v.string.context = 0;
}


static inline void mkPath(Value & v, const char * s)
{
    v.type = tPath;
    v.path = s;
}


typedef std::map<Path, PathSet> DrvRoots;
typedef std::map<Path, Hash> DrvHashes;

/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, Path> SrcToStore;

struct EvalState;


std::ostream & operator << (std::ostream & str, Value & v);


class EvalState 
{
    DrvRoots drvRoots;
    DrvHashes drvHashes; /* normalised derivation hashes */
    SrcToStore srcToStore; 

    unsigned long nrValues;
    unsigned long nrEnvs;
    unsigned long nrEvaluated;

    bool allowUnsafeEquality;

public:
    
    EvalState();

    /* Evaluate an expression read from the given file to normal
       form. */
    void evalFile(const Path & path, Value & v);

    /* Evaluate an expression to normal form, storing the result in
       value `v'. */
    void eval(Expr e, Value & v);
    void eval(Env & env, Expr e, Value & v);

    /* Evaluation the expression, then verify that it has the expected
       type. */
    bool evalBool(Env & env, Expr e);

    /* Evaluate an expression, and recursively evaluate list elements
       and attributes. */
    void strictEval(Expr e, Value & v);
    void strictEval(Env & env, Expr e, Value & v);

    /* If `v' is a thunk, enter it and overwrite `v' with the result
       of the evaluation of the thunk.  If `v' is a delayed function
       application, call the function and overwrite `v' with the
       result.  Otherwise, this is a no-op. */
    void forceValue(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    int forceInt(Value & v);
    void forceAttrs(Value & v);
    void forceList(Value & v);
    void forceFunction(Value & v); // either lambda or primop

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

    void createBaseEnv();
    
    void addConstant(const string & name, Value & v);

    void addPrimOp(const string & name,
        unsigned int arity, PrimOp primOp);

    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    void callFunction(Value & fun, Value & arg, Value & v);

public:
    
    /* Allocation primitives. */
    Value * allocValues(unsigned int count);
    Env & allocEnv();

    void mkList(Value & v, unsigned int length);

    /* Print statistics. */
    void printStats();
};


#if 0
/* Evaluate an expression to normal form. */
Expr evalExpr(EvalState & state, Expr e);

/* Evaluate an expression, and recursively evaluate list elements and
   attributes.  If `canonicalise' is true, we remove things like
   position information and make sure that attribute sets are in
   sorded order. */
Expr strictEvalExpr(EvalState & state, Expr e);

/* Specific results. */
string evalString(EvalState & state, Expr e, PathSet & context);
string evalStringNoCtx(EvalState & state, Expr e);
int evalInt(EvalState & state, Expr e);
bool evalBool(EvalState & state, Expr e);
ATermList evalList(EvalState & state, Expr e);

/* Flatten nested lists into a single list (or expand a singleton into
   a list). */
ATermList flattenList(EvalState & state, Expr e);

/* Automatically call a function for which each argument has a default
   value or has a binding in the `args' map.  Note: result is a call,
   not a normal form; it should be evaluated by calling evalExpr(). */
Expr autoCallFunction(Expr e, const ATermMap & args);
#endif


}


#endif /* !__EVAL_H */
