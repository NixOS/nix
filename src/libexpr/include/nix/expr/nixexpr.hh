#pragma once
///@file

#include <map>
#include <vector>
#include <memory_resource>

#include "nix/expr/gc-small-vector.hh"
#include "nix/expr/value.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/expr/eval-error.hh"
#include "nix/util/pos-idx.hh"
#include "nix/expr/counter.hh"

namespace nix {

class EvalState;
class PosTable;
struct Env;
struct ExprWith;
struct StaticEnv;
struct Value;

/**
 * A documentation comment, in the sense of [RFC
 * 145](https://github.com/NixOS/rfcs/blob/master/rfcs/0145-doc-strings.md)
 *
 * Note that this does not implement the following:
 *  - argument attribute names ("formals"): TBD
 *  - argument names: these are internal to the function and their names may not be optimal for documentation
 *  - function arity (degree of currying or number of ':'s):
 *      - Functions returning partially applied functions have a higher arity
 *        than can be determined locally and without evaluation.
 *        We do not want to present false data.
 *      - Some functions should be thought of as transformations of other
 *        functions. For instance `overlay -> overlay -> overlay` is the simplest
 *        way to understand `composeExtensions`, but its implementation looks like
 *        `f: g: final: prev: <...>`. The parameters `final` and `prev` are part
 *        of the overlay concept, while distracting from the function's purpose.
 */
struct DocComment
{

    /**
     * Start of the comment, including the opening, ie `/` and `**`.
     */
    PosIdx begin;

    /**
     * Position right after the final asterisk and `/` that terminate the comment.
     */
    PosIdx end;

    /**
     * Whether the comment is set.
     *
     * A `DocComment` is small enough that it makes sense to pass by value, and
     * therefore baking optionality into it is also useful, to avoiding the memory
     * overhead of `std::optional`.
     */
    operator bool() const
    {
        return static_cast<bool>(begin);
    }

    std::string getInnerText(const PosTable & positions) const;
};

/**
 * An attribute path is a sequence of attribute names.
 */
struct AttrName
{
    Symbol symbol;
    Expr * expr = nullptr;
    AttrName(Symbol s)
        : symbol(s) {};
    AttrName(Expr * e)
        : expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath);

using UpdateQueue = SmallTemporaryValueVector<conservativeStackReservation>;

class Exprs
{
    std::pmr::monotonic_buffer_resource buffer;
public:
    std::pmr::polymorphic_allocator<char> alloc{&buffer};
};

/* Abstract syntax of Nix expressions. */

struct Expr
{
    struct AstSymbols
    {
        Symbol sub, lessThan, mul, div, or_, findFile, nixPath, body;
    };

    static Counter nrCreated;

    Expr()
    {
        nrCreated++;
    }

    virtual ~Expr() {};
    virtual void show(const SymbolTable & symbols, std::ostream & str) const;
    virtual void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env);

    /** Normal evaluation, implemented directly by all subclasses. */
    virtual void eval(EvalState & state, Env & env, Value & v);

    /**
     * Create a thunk for the delayed computation of the given expression
     * in the given environment. But if the expression is a variable,
     * then look it up right away. This significantly reduces the number
     * of thunks allocated.
     */
    virtual Value * maybeThunk(EvalState & state, Env & env);

    /**
     * Only called when performing an attrset update: `//` or similar.
     * Instead of writing to a Value &, this function writes to an UpdateQueue.
     * This allows the expression to perform multiple updates in a delayed manner, gathering up all the updates before
     * applying them.
     */
    virtual void evalForUpdate(EvalState & state, Env & env, UpdateQueue & q, std::string_view errorCtx);
    virtual void setName(Symbol name);
    virtual void setDocComment(DocComment docComment) {};

    virtual PosIdx getPos() const
    {
        return noPos;
    }

    // These are temporary methods to be used only in parser.y
    virtual void resetCursedOr() {};
    virtual void warnIfCursedOr(const SymbolTable & symbols, const PosTable & positions) {};
};

#define COMMON_METHODS                                                         \
    void show(const SymbolTable & symbols, std::ostream & str) const override; \
    void eval(EvalState & state, Env & env, Value & v) override;               \
    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override;

struct ExprInt : Expr
{
    static Counter nrCreated;
    Value v;

    ExprInt(NixInt n)
    {
        nrCreated++;
        v.mkInt(n);
    };

    ExprInt(NixInt::Inner n)
    {
        nrCreated++;
        v.mkInt(n);
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprFloat : Expr
{
    static Counter nrCreated;
    Value v;

    ExprFloat(NixFloat nf)
    {
        nrCreated++;
        v.mkFloat(nf);
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprString : Expr
{
    static Counter nrCreated;
    Value v;

    /**
     * This is only for strings already allocated in our polymorphic allocator,
     * or that live at least that long (e.g. c++ string literals)
     */
    ExprString(const char * s)
    {
        nrCreated++;
        v.mkStringNoCopy(s);
    };

    ExprString(std::pmr::polymorphic_allocator<char> & alloc, std::string_view sv)
    {
        nrCreated++;
        auto len = sv.length();
        if (len == 0) {
            v.mkStringNoCopy("");
            return;
        }
        char * s = alloc.allocate(len + 1);
        sv.copy(s, len);
        s[len] = '\0';
        v.mkStringNoCopy(s);
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprPath : Expr
{
    static Counter nrCreated;
    ref<SourceAccessor> accessor;
    std::string s;
    Value v;

    ExprPath(ref<SourceAccessor> accessor, std::string s)
        : accessor(accessor)
        , s(std::move(s))
    {
        nrCreated++;
        v.mkPath(&*accessor, this->s.c_str());
    }

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

typedef uint32_t Level;
typedef uint32_t Displacement;

struct ExprVar : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Symbol name;

    /* Whether the variable comes from an environment (e.g. a rec, let
       or function argument) or from a "with".

       `nullptr`: Not from a `with`.
       Valid pointer: the nearest, innermost `with` expression to query first. */
    ExprWith * fromWith = nullptr;

    /* In the former case, the value is obtained by going `level`
       levels up from the current environment and getting the
       `displ`th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name` from
       the set stored in the environment that is `level` levels up
       from the current one.*/
    Level level = 0;
    Displacement displ = 0;

    ExprVar(Symbol name)
        : name(name)
    {
        nrCreated++;
    };

    ExprVar(const PosIdx & pos, Symbol name)
        : pos(pos)
        , name(name)
    {
        nrCreated++;
    };

    Value * maybeThunk(EvalState & state, Env & env) override;

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

/**
 * A pseudo-expression for the purpose of evaluating the `from` expression in `inherit (from)` syntax.
 * Unlike normal variable references, the displacement is set during parsing, and always refers to
 * `ExprAttrs::inheritFromExprs` (by itself or in `ExprLet`), whose values are put into their own `Env`.
 */
struct ExprInheritFrom : ExprVar
{
    static Counter nrCreated;

    ExprInheritFrom(PosIdx pos, Displacement displ)
        : ExprVar(pos, {})
    {
        nrCreated++;
        this->level = 0;
        this->displ = displ;
        this->fromWith = nullptr;
    }

    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override;
};

struct ExprSelect : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Expr *e, *def;
    AttrPath attrPath;

    ExprSelect(const PosIdx & pos, Expr * e, AttrPath attrPath, Expr * def)
        : pos(pos)
        , e(e)
        , def(def)
        , attrPath(std::move(attrPath))
    {
        nrCreated++;
    };

    ExprSelect(const PosIdx & pos, Expr * e, Symbol name)
        : pos(pos)
        , e(e)
        , def(0)
    {
        nrCreated++;
        attrPath.push_back(AttrName(name));
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    /**
     * Evaluate the `a.b.c` part of `a.b.c.d`. This exists mostly for the purpose of :doc in the repl.
     *
     * @param[out] attrs The attribute set that should contain the last attribute name (if it exists).
     * @return The last attribute name in `attrPath`
     *
     * @note This does *not* evaluate the final attribute, and does not fail if that's the only attribute that does not
     * exist.
     */
    Symbol evalExceptFinalSelect(EvalState & state, Env & env, Value & attrs);

    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    static Counter nrCreated;
    Expr * e;
    AttrPath attrPath;

    ExprOpHasAttr(Expr * e, AttrPath attrPath)
        : e(e)
        , attrPath(std::move(attrPath))
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return e->getPos();
    }

    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    static Counter nrCreated;
    bool recursive;
    PosIdx pos;

    struct AttrDef
    {
        enum class Kind {
            /** `attr = expr;` */
            Plain,
            /** `inherit attr1 attrn;` */
            Inherited,
            /** `inherit (expr) attr1 attrn;` */
            InheritedFrom,
        };

        Kind kind;
        Expr * e;
        PosIdx pos;
        Displacement displ = 0; // displacement

        AttrDef(Expr * e, const PosIdx & pos, Kind kind = Kind::Plain)
            : kind(kind)
            , e(e)
            , pos(pos)
        {
            nrCreated++;
        };

        AttrDef()
        {
            nrCreated++;
        };

        template<typename T>
        const T & chooseByKind(const T & plain, const T & inherited, const T & inheritedFrom) const
        {
            switch (kind) {
            case Kind::Plain:
                return plain;
            case Kind::Inherited:
                return inherited;
            default:
            case Kind::InheritedFrom:
                return inheritedFrom;
            }
        }
    };

    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    std::unique_ptr<std::vector<Expr *>> inheritFromExprs;

    struct DynamicAttrDef
    {
        Expr *nameExpr, *valueExpr;
        PosIdx pos;
        DynamicAttrDef(Expr * nameExpr, Expr * valueExpr, const PosIdx & pos)
            : nameExpr(nameExpr)
            , valueExpr(valueExpr)
            , pos(pos) {};
    };

    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;
    ExprAttrs(const PosIdx & pos)
        : recursive(false)
        , pos(pos) {};
    ExprAttrs()
        : recursive(false) {};

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS

    std::shared_ptr<const StaticEnv> bindInheritSources(EvalState & es, const std::shared_ptr<const StaticEnv> & env);
    Env * buildInheritFromEnv(EvalState & state, Env & up);
    void showBindings(const SymbolTable & symbols, std::ostream & str) const;
};

struct ExprList : Expr
{
    static Counter nrCreated;
    std::vector<Expr *> elems;

    ExprList()
    {
        nrCreated++;
    };

    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env) override;

    PosIdx getPos() const override
    {
        return elems.empty() ? noPos : elems.front()->getPos();
    }
};

struct Formal
{
    PosIdx pos;
    Symbol name;
    Expr * def;
};

struct Formals
{
    typedef std::vector<Formal> Formals_;
    /**
     * @pre Sorted according to predicate (std::tie(a.name, a.pos) < std::tie(b.name, b.pos)).
     */
    Formals_ formals;
    bool ellipsis;

    bool has(Symbol arg) const
    {
        auto it = std::lower_bound(
            formals.begin(), formals.end(), arg, [](const Formal & f, const Symbol & sym) { return f.name < sym; });
        return it != formals.end() && it->name == arg;
    }

    std::vector<Formal> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<Formal> result(formals.begin(), formals.end());
        std::sort(result.begin(), result.end(), [&](const Formal & a, const Formal & b) {
            std::string_view sa = symbols[a.name], sb = symbols[b.name];
            return sa < sb;
        });
        return result;
    }
};

struct ExprLambda : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Symbol name;
    Symbol arg;
    Formals * formals;
    Expr * body;
    DocComment docComment;

    ExprLambda(PosIdx pos, Symbol arg, Formals * formals, Expr * body)
        : pos(pos)
        , arg(arg)
        , formals(formals)
        , body(body)
    {
        nrCreated++;
    };

    ExprLambda(PosIdx pos, Formals * formals, Expr * body)
        : pos(pos)
        , formals(formals)
        , body(body)
    {
        nrCreated++;
    }

    void setName(Symbol name) override;
    std::string showNamePos(const EvalState & state) const;

    inline bool hasFormals() const
    {
        return formals != nullptr;
    }

    PosIdx getPos() const override
    {
        return pos;
    }

    virtual void setDocComment(DocComment docComment) override;
    COMMON_METHODS
};

struct ExprCall : Expr
{
    static Counter nrCreated;
    Expr * fun;
    std::vector<Expr *> args;
    PosIdx pos;
    std::optional<PosIdx> cursedOrEndPos; // used during parsing to warn about https://github.com/NixOS/nix/issues/11118

    ExprCall(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args)
        : fun(fun)
        , args(args)
        , pos(pos)
        , cursedOrEndPos({})
    {
        nrCreated++;
    }

    ExprCall(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args, PosIdx && cursedOrEndPos)
        : fun(fun)
        , args(args)
        , pos(pos)
        , cursedOrEndPos(cursedOrEndPos)
    {
        nrCreated++;
    }

    PosIdx getPos() const override
    {
        return pos;
    }

    virtual void resetCursedOr() override;
    virtual void warnIfCursedOr(const SymbolTable & symbols, const PosTable & positions) override;
    COMMON_METHODS
};

struct ExprLet : Expr
{
    static Counter nrCreated;
    ExprAttrs * attrs;
    Expr * body;

    ExprLet(ExprAttrs * attrs, Expr * body)
        : attrs(attrs)
        , body(body)
    {
        nrCreated++;
    };

    COMMON_METHODS
};

struct ExprWith : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Expr *attrs, *body;
    size_t prevWith;
    ExprWith * parentWith;

    ExprWith(const PosIdx & pos, Expr * attrs, Expr * body)
        : pos(pos)
        , attrs(attrs)
        , body(body)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprIf : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Expr *cond, *then, *else_;

    ExprIf(const PosIdx & pos, Expr * cond, Expr * then, Expr * else_)
        : pos(pos)
        , cond(cond)
        , then(then)
        , else_(else_)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprAssert : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    Expr *cond, *body;

    ExprAssert(const PosIdx & pos, Expr * cond, Expr * body)
        : pos(pos)
        , cond(cond)
        , body(body)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    static Counter nrCreated;
    Expr * e;

    ExprOpNot(Expr * e)
        : e(e)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return e->getPos();
    }

    COMMON_METHODS
};

#define MakeBinOpMembers(name, s)                                                        \
    static Counter nrCreated;                                                            \
    PosIdx pos;                                                                          \
    Expr *e1, *e2;                                                                       \
    name(Expr * e1, Expr * e2)                                                           \
        : e1(e1)                                                                         \
        , e2(e2)                                                                         \
    {                                                                                    \
        nrCreated++;                                                                     \
    };                                                                                   \
    name(const PosIdx & pos, Expr * e1, Expr * e2)                                       \
        : pos(pos)                                                                       \
        , e1(e1)                                                                         \
        , e2(e2)                                                                         \
    {                                                                                    \
        nrCreated++;                                                                     \
    };                                                                                   \
    void show(const SymbolTable & symbols, std::ostream & str) const override            \
    {                                                                                    \
        str << "(";                                                                      \
        e1->show(symbols, str);                                                          \
        str << " " s " ";                                                                \
        e2->show(symbols, str);                                                          \
        str << ")";                                                                      \
    }                                                                                    \
    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override \
    {                                                                                    \
        e1->bindVars(es, env);                                                           \
        e2->bindVars(es, env);                                                           \
    }                                                                                    \
    void eval(EvalState & state, Env & env, Value & v) override;                         \
    PosIdx getPos() const override                                                       \
    {                                                                                    \
        return pos;                                                                      \
    }

#define MakeBinOp(name, s)        \
    struct name : Expr            \
    {                             \
        MakeBinOpMembers(name, s) \
    }

MakeBinOp(ExprOpEq, "==");
MakeBinOp(ExprOpNEq, "!=");
MakeBinOp(ExprOpAnd, "&&");
MakeBinOp(ExprOpOr, "||");
MakeBinOp(ExprOpImpl, "->");
MakeBinOp(ExprOpConcatLists, "++");

struct ExprOpUpdate : Expr
{
private:
    /** Special case for merging of two attrsets. */
    void eval(EvalState & state, Value & v, Value & v1, Value & v2);
    void evalForUpdate(EvalState & state, Env & env, UpdateQueue & q);

public:
    MakeBinOpMembers(ExprOpUpdate, "//");
    virtual void evalForUpdate(EvalState & state, Env & env, UpdateQueue & q, std::string_view errorCtx) override;
};

struct ExprConcatStrings : Expr
{
    static Counter nrCreated;
    PosIdx pos;
    bool forceString;
    std::vector<std::pair<PosIdx, Expr *>> * es;

    ExprConcatStrings(const PosIdx & pos, bool forceString, std::vector<std::pair<PosIdx, Expr *>> * es)
        : pos(pos)
        , forceString(forceString)
        , es(es)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprPos : Expr
{
    static Counter nrCreated;
    PosIdx pos;

    ExprPos(const PosIdx & pos)
        : pos(pos)
    {
        nrCreated++;
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

/* only used to mark thunks as black holes. */
struct ExprBlackHole : Expr
{
    void show(const SymbolTable & symbols, std::ostream & str) const override {}

    void eval(EvalState & state, Env & env, Value & v) override;

    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override {}

    [[noreturn]] static void throwInfiniteRecursionError(EvalState & state, Value & v);
};

extern ExprBlackHole eBlackHole;

/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    ExprWith * isWith;
    std::shared_ptr<const StaticEnv> up;

    // Note: these must be in sorted order.
    typedef std::vector<std::pair<Symbol, Displacement>> Vars;
    Vars vars;

    StaticEnv(ExprWith * isWith, std::shared_ptr<const StaticEnv> up, size_t expectedSize = 0)
        : isWith(isWith)
        , up(std::move(up))
    {
        vars.reserve(expectedSize);
    };

    void sort()
    {
        std::stable_sort(vars.begin(), vars.end(), [](const Vars::value_type & a, const Vars::value_type & b) {
            return a.first < b.first;
        });
    }

    void deduplicate()
    {
        auto it = vars.begin(), jt = it, end = vars.end();
        while (jt != end) {
            *it = *jt++;
            while (jt != end && it->first == jt->first)
                *it = *jt++;
            it++;
        }
        vars.erase(it, end);
    }

    Vars::const_iterator find(Symbol name) const
    {
        Vars::value_type key(name, 0);
        auto i = std::lower_bound(vars.begin(), vars.end(), key);
        if (i != vars.end() && i->first == name)
            return i;
        return vars.end();
    }
};

} // namespace nix
