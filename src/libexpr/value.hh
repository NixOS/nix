#pragma once

#include "symbol-table.hh"

namespace nix {


class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct PrimOp;
struct PrimOp;
class Symbol;
struct Pos;
class EvalState;
class XMLWriter;


typedef long NixInt;

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

    /* How much space does this value take up */
    virtual size_t valueSize(std::set<const void *> & seen) const = 0;

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
        std::ostream & str, PathSet & context) const;

    /* Print the value as XML. Defaults to unevaluated */
    virtual void printValueAsXML(EvalState & state, bool strict, bool location,
        XMLWriter & doc, PathSet & context, PathSet & drvsSeen) const;

    virtual ~ExternalValueBase()
    {
    };
};

std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);

struct Value
{
private:
    struct Storage {
        uintptr_t first;
        uintptr_t second;
    };

    Storage raw;

    /* In order to avoid having too many tagged pointers, we tag both the first pointer and the second pointer. */
    enum LowFirstTag {
        /* First is a GC pointer */
        LFT_0 = 0,
        LFT_1 = 1,
        LFT_2 = 2, /* unused */

        /* constant value, or single value stored in the second member. */
        LFT_3 = 3, /* not a GC pointer */

        /* Meta */
        LFT_MASK = 3,
        LFT_MAX = 4
    };

    enum HighFirstTag {
        HFT_Blackhole = 0,
        HFT_Null,
        HFT_Int,
        HFT_Bool,
        HFT_Path,
        HFT_Attrs,
        HFT_List0,
        HFT_List1,
        HFT_PrimOp,
        HFT_External,

        HFT_NotAValue,
    };

    enum LowSecondTag {
        /* LFT_0 */
        LST_PrimOpApp = 0,
        LST_App = 1,
        LST_List2 = 2,
        LST_ListN = 3,

        /* LFT_1 */
        LST_String = 0,
        LST_Thunk = 1,
        LST_Lambda = 2,
        /* unused */

        /* LFT_2 */
        /* unused */
        /* unused */
        /* unused */
        /* unused */

        /* Meta */
        LST_MASK = 3,
        LST_MAX = 4
    };

    template <typename T>
    void setFirst(LowFirstTag low, const T& val) {
        uintptr_t ival = uintptr_t(val);
        assert((ival & LFT_MASK) == 0);
        /* If the following assertion fails, then we should change the
           initGC function, to register more known displacements. */
        assert(!std::is_pointer<T>::value || uintptr_t(low) <= 2);
        raw.first = low | ival;
    }
    void setFirstHigh(enum HighFirstTag val) {
        setFirst(LFT_3, uintptr_t(val) * LFT_MAX);
    }
    template <typename T>
    T getFirst(LowFirstTag low) const {
        assert((raw.first & LFT_MASK) == low);
        return T(raw.first & ~LFT_MASK);
    }

    template <typename T>
    void setSecond(const T& val) {
        raw.second = uintptr_t(val);
    }
    template <typename T>
    void setSecond(LowSecondTag low, const T& val) {
        uintptr_t ival = uintptr_t(val);
        assert((ival & LFT_MASK) == 0);
        /* If the following assertion fails, then we should change the
           initGC function, to register more known displacements. */
        assert(!std::is_pointer<T>::value || uintptr_t(low) <= 2);
        raw.second = low | ival;
    }
    template <typename T>
    T getSecond() const {
        return T(raw.second);
    }
    template <typename T>
    T getSecond(LowSecondTag low) const {
        assert((raw.second & LST_MASK) == low);
        return T(raw.second & ~LST_MASK);
    }
    template <typename T>
    T getSecondUnchecked() const {
        return T(raw.second & ~LST_MASK);
    }

public:
    /* The current packing scheme implies that we have to accept inner
       pointers with offsets 1 and 2. The low bits of the first and second
       value are reserved to identify the type of the value. As we have to
       inform the GC about known inner pointers, we reserve the low bits
       between 0 and 2 for GC pointers. */
    enum Type {
        /* First is a GC pointer. */
        tPrimOpApp = LFT_0 | LFT_MAX * LST_PrimOpApp,
        tApp       = LFT_0 | LFT_MAX * LST_App,
        tList2     = LFT_0 | LFT_MAX * LST_List2,
        tListN     = LFT_0 | LFT_MAX * LST_ListN,

        tString    = LFT_1 | LFT_MAX * LST_String,
        tThunk     = LFT_1 | LFT_MAX * LST_Thunk,
        tLambda    = LFT_1 | LFT_MAX * LST_Lambda,
        /* unused */

        /* unused */
        /* unused */
        /* unused */
        /* unused */

        tHighFirstTag = LFT_MAX | LFT_MAX * LST_MAX,

        /* First is not a GC pointer. */
        tBlackhole = HFT_Blackhole + tHighFirstTag,
        tNull      = HFT_Null + tHighFirstTag,
        tInt       = HFT_Int + tHighFirstTag,
        tBool      = HFT_Bool + tHighFirstTag,
        tPath      = HFT_Path + tHighFirstTag,
        tAttrs     = HFT_Attrs + tHighFirstTag,
        tList0     = HFT_List0 + tHighFirstTag,
        tList1     = HFT_List1 + tHighFirstTag,
        tPrimOp    = HFT_PrimOp + tHighFirstTag,
        tExternal  = HFT_External + tHighFirstTag,

        /* tNotAValue = HFT_NotAValue + tHighFirstTag, */
    };

    Type type() const {
        if ((raw.first & LFT_MASK) == LFT_3)
            return Type(raw.first / LFT_MAX + tHighFirstTag);
        return Type((raw.first & LFT_MASK) | LFT_MAX * (raw.second & LST_MASK));
    }
    bool isList() const {
        Type t = type();
        return t == tList0 || t == tList1 || t == tList2 || t == tListN;
    }

    void setInt(NixInt integer) {
        setFirstHigh(HFT_Int);
        setSecond(integer);
        assert(type() == tInt);
    }
    NixInt asInt() const {
        assert(type() == tInt);
        return getSecond<NixInt>();
    }

    void setBool(bool boolean) {
        setFirstHigh(HFT_Bool);
        setSecond(boolean);
        assert(type() == tBool);
    }
    bool asBool() const {
        assert(type() == tBool);
        return getSecond<bool>();
    }

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
         the inputSrcs of the derivations.

         For canonicity, the store paths should be in sorted order. */
    void setStringNoCopy(const char * str, const char * * context) {
        setFirst(LFT_1, str);
        setSecond(LST_String, context);
        assert(type() == tString);
    }
    void setString(const Symbol & s) {
        setStringNoCopy(((const string &) s).c_str(), nullptr);
    }
    void setString(const char * str);
    void setString(const string & s) {
        setString(s.c_str());
    }
    void setString(const string & s, const PathSet & context);
    const char * asString() const {
        assert(type() == tString);
        return getFirst<const char *>(LFT_1);
    }
    const char * * asStringContext() const {
        assert(type() == tString);
        return getSecond<const char * *>(LST_String);
    }

    void setPathNoCopy(const char * path) {
        setFirstHigh(HFT_Path);
        setSecond(path);
        assert(type() == tPath);
    }
    void setPath(const char * path);
    const char * asPath() const {
        assert(type() == tPath);
        return getSecond<const char *>();
    }

    void setAttrs(Bindings * attrs) {
        setFirstHigh(HFT_Attrs);
        setSecond(attrs);
        assert(type() == tAttrs);
    }
    Bindings * asAttrs() const {
        assert(type() == tAttrs);
        return getSecond<Bindings *>();
    }

    void setList() {
        setFirstHigh(HFT_List0);
        assert(type() == tList0);
    }
    void setList(Value * e0) {
        setFirstHigh(HFT_List1);
        setSecond(e0);
        assert(type() == tList1);
    }
    void setList(Value * e0, Value * e1) {
        setFirst(LFT_0, e0);
        setSecond(LST_List2, e1);
        assert(type() == tList2);
    }
    void setList(Value * * elems, size_t size) {
        setFirst(LFT_0, elems);
        setSecond(LST_ListN, size * LST_MAX);
        assert(type() == tListN);
    }

    class asList {
    protected:
        Value * * list_;
        size_t length_;
        Value * smallList_[2];

        void init(const Value * v) {
            Type t = v->type();
            if (t == tList0) {
                length_ = 0;
                list_ = smallList_;
            } else if (t == tList1) {
                length_ = 1;
                list_ = smallList_;
                smallList_[0] = v->getSecond<Value *>();
            } else if (t == tList2) {
                length_ = 2;
                list_ = smallList_;
                smallList_[0] = v->getFirst<Value *>(LFT_0);
                smallList_[1] = v->getSecond<Value *>(LST_List2);
            } else {
                assert(t == tListN);
                length_ = v->getSecond<size_t>(LST_ListN) / LST_MAX;;
                list_ = v->getFirst<Value * *>(LFT_0);
            }
        }

    public:
        asList(const Value * v) {
            init(v);
        }
        asList(const Value & v) {
            init(&v);
        }
        ~asList() {
        }

        size_t length() const {
            return length_;
        }
        const Value * const * begin() const {
            return list_;
        }
        Value * * begin() {
            return list_;
        }
        const Value * const * end() const {
            return list_ + length_;
        }
        Value * * end() {
            return list_ + length_;
        }

        const Value * operator[] (size_t i) const {
            return list_[i];
        }
        Value * & operator[] (size_t i) {
            return list_[i];
        }
    };
    friend class asList;

    class asMutableList : public asList {
        Value * v_;

    public:
        asMutableList(Value * v)
          : asList(v),
            v_(v)
        {
#ifdef DEBUG
            v->setBlackhole()
#endif
        }
        asMutableList(Value & v)
          : asList(&v),
            v_(&v)
        {
#ifdef DEBUG
            v.setBlackhole()
#endif
        }
        ~asMutableList() {
            synchronizeValueContent();
        }

        void synchronizeValueContent() {
            if (length_ == 1)
                v_->setList(list_[0]);
            else if (length_ == 2)
                v_->setList(list_[0], list_[1]);
#ifdef DEBUG
            /* If we are not in debug mode, then the content would be the
               same as before. */
            else if (length_ == 0)
                setList();
            else
                setList(list_, length_);
#endif
        }
    };

    void setPrimOpApp(Value * left, Value * right) {
        setFirst(LFT_0, left);
        setSecond(LST_PrimOpApp, right);
        assert(type() == tPrimOpApp);
    }
    void setApp(Value & left, Value & right) {
        setFirst(LFT_0, &left);
        setSecond(LST_App, &right);
        assert(type() == tApp);
    }
    Value * asAppLeft() const {
        assert(type() == tApp || type() == tPrimOpApp);
        return getFirst<Value *>(LFT_0);
    }
    Value * asAppRight() const {
        assert(type() == tApp || type() == tPrimOpApp);
        return getSecondUnchecked<Value *>();
    }

    void setThunk(Env * env, Expr * expr) {
        setFirst(LFT_1, env);
        setSecond(LST_Thunk, expr);
        assert(type() == tThunk);
    }
    void setLambda(Env * env, ExprLambda * fun) {
        setFirst(LFT_1, env);
        setSecond(LST_Lambda, fun);
        assert(type() == tLambda);
    }
    Env * asExprEnv() const {
        assert(type() == tThunk || type() == tLambda);
        return getFirst<Env *>(LFT_1);
    }
    Expr * asThunk() const {
        assert(type() == tThunk);
        return getSecond<Expr *>(LST_Thunk);
    }
    ExprLambda * asLambda() const {
        assert(type() == tLambda);
        return getSecond<ExprLambda *>(LST_Lambda);
    }

    void setPrimOp(PrimOp * primop) {
        setFirstHigh(HFT_PrimOp);
        setSecond(primop);
        assert(type() == tPrimOp);
    }
    PrimOp * asPrimOp() const {
        assert(type() == tPrimOp);
        return getSecond<PrimOp *>();
    }

    void setExternal(ExternalValueBase * external) {
        setFirstHigh(HFT_External);
        setSecond(external);
        assert(type() == tExternal);
    }
    ExternalValueBase * asExternal() const {
        assert(type() == tExternal);
        return getSecond<ExternalValueBase *>();
    }

    void setBlackhole() {
        setFirstHigh(HFT_Blackhole);
        assert(type() == tBlackhole);
    }
    void setNull() {
        setFirstHigh(HFT_Null);
        assert(type() == tNull);
    }

    void clear() {
        setFirstHigh(HFT_NotAValue);
    }
};


/* Compute the size in bytes of the given value, including all values
   and environments reachable from it. Static expressions (Exprs) are
   not included. */
size_t valueSize(Value & v);


}
