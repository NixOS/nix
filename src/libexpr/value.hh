#pragma once

#include "symbol-table.hh"
#include "gc.hh"

#include <cstring>

namespace nix {

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct PrimOp;
class Symbol;
struct Pos;
class EvalState;
class XMLWriter;
class JSONPlaceholder;


typedef int64_t NixInt;
typedef double NixFloat;

#if 0
/* External values must descend from ExternalValueBase, so that
 * type-agnostic nix functions (e.g. showType) can be implemented
 */
class ExternalValueBase
{
    friend std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);

protected:
    /* Print out the value */
    virtual std::ostream & print(std::ostream & str) const = 0;

    public:
    /* Return a simple string describing the type */
    virtual string showType() const = 0;

    /* Return a string to be used in builtins.typeOf */
    virtual string typeOf() const = 0;

    /* Coerce the value to a string. Defaults to uncoercable, i.e. throws an
     * error
     */
    virtual string coerceToString(const Pos & pos, PathSet & context, bool copyMore, bool copyToStore) const;

    /* Compare to another value of the same type. Defaults to uncomparable,
     * i.e. always false.
     */
    virtual bool operator==(const ExternalValueBase & b) const;

    /* Print the value as JSON. Defaults to unconvertable, i.e. throws an error */
    virtual void printValueAsJSON(EvalState & state, bool strict,
        JSONPlaceholder & out, PathSet & context) const;

    /* Print the value as XML. Defaults to unevaluated */
    virtual void printValueAsXML(EvalState & state, bool strict, bool location,
        XMLWriter & doc, PathSet & context, PathSet & drvsSeen) const;

    virtual ~ExternalValueBase()
    {
    };
};

std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);
#endif


class Context : Object
{
    friend class Value;
    friend class GC;

    Symbol members[0];

    Context(const PathSet & context) : Object(tContext, context.size())
    {
        size_t n = 0;
        for (auto & i : context)
            members[n++] = symbols.create(i);
    }

    size_t getSize() { return getMisc(); }
};


struct Value : Object
{
    union
    {
        NixInt integer;
        bool boolean;

        /* Strings in the evaluator carry a so-called `context' which
           is a list of strings representing store paths.  This is to
           allow users to write things like

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
           the inputSrcs of the derivations. */
        struct {
            String * s;
            Context * context;
        } string;
        const char * staticString;
        String * path;
        Bindings * attrs;
        PtrList<Value> * bigList;
        Value * smallList[2];
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
        //ExternalValueBase * external;
        NixFloat fpoint;
    };

private:

    Value() : Object(tNull, 0) {}

    friend class GC;
    template<typename T> friend class Root;

public:

    bool isList() const
    {
        return type >= tList0 && type <= tListN;
    }

    Value * * listElems()
    {
        return type == tList0 || type == tList1 || type == tList2 ? smallList : bigList->elems;
    }

    const Value * const * listElems() const
    {
        return type == tList0 || type == tList1 || type == tList2 ? smallList : bigList->elems;
    }

    size_t listSize() const
    {
        return type == tList0 ? 0 : type == tList1 ? 1 : type == tList2 ? 2 : bigList->size();
    }

    constexpr static size_t words() { return 3; } // FIXME

    void setContext(const PathSet & context)
    {
        if (context.size() == 0)
            string.context = nullptr;
        else if (context.size() == 1) {
            // If we have a single context, then store it
            // directly. This saves allocating a Context object (16
            // bytes).
            auto symbol = symbols.create(*context.begin());
            string.context = (Context *) (((ptrdiff_t) symbol.s) | 1);
        } else {
            string.context = gc.alloc<Context>(1 + context.size(), context);
        }
    }

    void getContext(PathSet & context)
    {
        if (type == tLongString && string.context) {
            if (((ptrdiff_t) string.context) & 1) {
                auto s = (const std::string *) (((ptrdiff_t) string.context) & ~1UL);
                context.insert(*s);
            } else {
                auto size = string.context->getSize();
                for (size_t i = 0; i < size; ++i)
                    context.insert(string.context->members[i]);
            }
        }
    }

    bool isString() const
    {
        return type == tShortString || type == tStaticString || type == tLongString;
    }

    void setShortString(std::string_view s)
    {
        // FIXME: can't use strcpy here because gcc flags it as a
        // buffer overflow on 'misc'.
        auto p = getMiscData();
        memcpy(p, s.data(), s.size());
        p[s.size()] = 0;
        type = tShortString;
    }

    const char * getString() const
    {
        if (type == tShortString)
            return getMiscData();
        else if (type == tStaticString)
            return staticString;
        else
            return string.s->s;
    }
};


static inline void mkInt(Value & v, NixInt n)
{
    v.type = tInt;
    v.integer = n;
}


static inline void mkFloat(Value & v, NixFloat n)
{
    v.type = tFloat;
    v.fpoint = n;
}


static inline void mkBool(Value & v, bool b)
{
    v.type = tBool;
    v.boolean = b;
}


static inline void mkNull(Value & v)
{
    v.type = tNull;
}


static inline void mkApp(Value & v, Value & left, Value & right)
{
    v.type = tApp;
    v.app.left = &left;
    v.app.right = &right;
}


static inline void mkPrimOpApp(Value & v, Value & left, Value & right)
{
    v.type = tPrimOpApp;
    v.app.left = &left;
    v.app.right = &right;
}


static inline Value & mkString(Value & v, std::string_view s)
{
    if (s.size() < WORD_SIZE * 2 + Object::miscBytes)
        v.setShortString(s);
    else {
        v.string.s = gc.alloc<String>(String::wordsFor(s.size()), s.size(), s.data());
        v.string.context = 0;
        v.type = tLongString;
    }
    return v;
}


static inline void mkString(Value & v, const Symbol & s)
{
    v.staticString = ((const string &) s).c_str();
    v.type = tStaticString;
}


static inline void mkPath(Value & v, const std::string & s)
{
    v.path = String::alloc(s.c_str());
    v.type = tPath;
}


typedef std::vector<Ptr<Value>> ValueVector; // FIXME: make more efficient
typedef std::map<Symbol, Ptr<Value>> ValueMap; // FIXME: use Bindings?


}
