#pragma once

#include "symbol-table.hh"

namespace nix {


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
    tBlackhole,
    tPrimOp,
    tPrimOpApp,
} ValueType;


struct Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct PrimOp;
struct PrimOp;
struct Symbol;


struct Value
{
    ValueType type;
    union 
    {
        int integer;
        bool boolean;
        
        /* Strings in the evaluator carry a so-called `context' (the
           ATermList) which is a list of strings representing store
           paths.  This is to allow users to write things like

             "--with-freetype2-library=" + freetype + "/lib"

           where `freetype' is a derivation (or a source to be copied
           to the store).  If we just concatenated the strings without
           keeping track of the referenced store paths, then if the
           string is used as a derivation attribute, the derivation
           will not have the correct dependencies in its inputDrvs and
           inputSrcs.

           The semantics of the context is as follows: when a string
           with context C is used as a derivation attribute, then the
           derivations in C will be added to the inputDrvs of the
           derivation, and the other store paths in C will be added to
           the inputSrcs of the derivations.

           For canonicity, the store paths should be in sorted order. */
        struct {
            const char * s;
            const char * * context; // must be in sorted order
        } string;
        
        const char * path;
        Bindings * attrs;
        struct {
            unsigned int length;
            Value * * elems;
        } list;
        struct {
            Env * env;
            Expr * expr;
        } thunk;
        struct {
            Value * left, * right;
        } app;
        struct {
            Env * env;
            ExprLambda * fun;
        } lambda;
        PrimOp * primOp;
        struct {
            Value * left, * right;
        } primOpApp;
    };
};


/* After overwriting an app node, be sure to clear pointers in the
   Value to ensure that the target isn't kept alive unnecessarily. */
static inline void clearValue(Value & v)
{
    v.app.right = 0;
}


static inline void mkInt(Value & v, int n)
{
    clearValue(v);
    v.type = tInt;
    v.integer = n;
}


static inline void mkBool(Value & v, bool b)
{
    clearValue(v);
    v.type = tBool;
    v.boolean = b;
}


static inline void mkApp(Value & v, Value & left, Value & right)
{
    v.type = tApp;
    v.app.left = &left;
    v.app.right = &right;
}


static inline void mkStringNoCopy(Value & v, const char * s)
{
    v.type = tString;
    v.string.s = s;
    v.string.context = 0;
}


static inline void mkString(Value & v, const Symbol & s)
{
    mkStringNoCopy(v, ((const string &) s).c_str());
}


void mkString(Value & v, const char * s);


static inline void mkPathNoCopy(Value & v, const char * s)
{
    clearValue(v);
    v.type = tPath;
    v.path = s;
}


void mkPath(Value & v, const char * s);


}
