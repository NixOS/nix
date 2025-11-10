#pragma once
///@file

#include <map>
#include <span>
#include <memory>
#include <vector>
#include <memory_resource>
#include <algorithm>

#include "nix/expr/gc-small-vector.hh"
#include "nix/expr/value.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/static-string-data.hh"
#include "nix/util/pos-idx.hh"
#include "nix/expr/counter.hh"
#include "nix/util/pos-table.hh"
#include "nix/util/error.hh"

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

static_assert(std::is_trivially_copy_constructible_v<AttrName>);

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const SymbolTable & symbols, std::span<const AttrName> attrPath);

using UpdateQueue = SmallTemporaryValueVector<conservativeStackReservation>;

/* Abstract syntax of Nix expressions. */

struct Expr
{
    struct AstSymbols
    {
        Symbol sub, lessThan, mul, div, or_, findFile, nixPath, body;
    };

    static Counter nrExprs;

    Expr()
    {
        nrExprs++;
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
    Value v;

    ExprInt(NixInt n)
    {
        v.mkInt(n);
    };

    ExprInt(NixInt::Inner n)
    {
        v.mkInt(n);
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprFloat : Expr
{
    Value v;

    ExprFloat(NixFloat nf)
    {
        v.mkFloat(nf);
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprString : Expr
{
    Value v;

    /**
     * This is only for strings already allocated in our polymorphic allocator,
     * or that live at least that long (e.g. c++ string literals)
     */
    ExprString(const StringData & s)
    {
        v.mkStringNoCopy(s);
    };

    ExprString(std::pmr::polymorphic_allocator<char> & alloc, std::string_view sv)
    {
        if (sv.size() == 0) {
            v.mkStringNoCopy(""_sds);
            return;
        }
        v.mkStringNoCopy(StringData::make(*alloc.resource(), sv));
    };

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprPath : Expr
{
    ref<SourceAccessor> accessor;
    Value v;

    ExprPath(std::pmr::polymorphic_allocator<char> & alloc, ref<SourceAccessor> accessor, std::string_view sv)
        : accessor(accessor)
    {
        v.mkPath(&*accessor, StringData::make(*alloc.resource(), sv));
    }

    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

typedef uint32_t Level;
typedef uint32_t Displacement;

struct ExprVar : Expr
{
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
        : name(name) {};
    ExprVar(const PosIdx & pos, Symbol name)
        : pos(pos)
        , name(name) {};
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
    ExprInheritFrom(PosIdx pos, Displacement displ)
        : ExprVar(pos, {})
    {
        this->level = 0;
        this->displ = displ;
        this->fromWith = nullptr;
    }

    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override;
};

struct ExprSelect : Expr
{
    PosIdx pos;
    uint32_t nAttrPath;
    Expr *e, *def;
    AttrName * attrPathStart;

    ExprSelect(
        std::pmr::polymorphic_allocator<char> & alloc,
        const PosIdx & pos,
        Expr * e,
        std::span<const AttrName> attrPath,
        Expr * def)
        : pos(pos)
        , nAttrPath(attrPath.size())
        , e(e)
        , def(def)
        , attrPathStart(alloc.allocate_object<AttrName>(nAttrPath))
    {
        std::ranges::copy(attrPath, attrPathStart);
    };

    ExprSelect(std::pmr::polymorphic_allocator<char> & alloc, const PosIdx & pos, Expr * e, Symbol name)
        : pos(pos)
        , nAttrPath(1)
        , e(e)
        , def(0)
        , attrPathStart((alloc.allocate_object<AttrName>()))
    {
        *attrPathStart = AttrName(name);
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    std::span<const AttrName> getAttrPath() const
    {
        return {attrPathStart, nAttrPath};
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
    Expr * e;
    std::span<AttrName> attrPath;

    ExprOpHasAttr(std::pmr::polymorphic_allocator<char> & alloc, Expr * e, std::span<AttrName> attrPath)
        : e(e)
        , attrPath({alloc.allocate_object<AttrName>(attrPath.size()), attrPath.size()})
    {
        std::ranges::copy(attrPath, this->attrPath.begin());
    };

    PosIdx getPos() const override
    {
        return e->getPos();
    }

    COMMON_METHODS
};

struct ExprAttrs : Expr
{
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
            , pos(pos) {};
        AttrDef() {};

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
    std::span<Expr *> elems;

    ExprList(std::pmr::polymorphic_allocator<char> & alloc, std::span<Expr *> exprs)
        : elems({alloc.allocate_object<Expr *>(exprs.size()), exprs.size()})
    {
        std::ranges::copy(exprs, elems.begin());
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

struct FormalsBuilder
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
};

struct Formals
{
    std::span<Formal> formals;
    bool ellipsis;

    Formals(std::span<Formal> formals, bool ellipsis)
        : formals(formals)
        , ellipsis(ellipsis) {};

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
    PosIdx pos;
    Symbol name;
    Symbol arg;

private:
    bool hasFormals;
    bool ellipsis;
    uint16_t nFormals;
    Formal * formalsStart;
public:

    std::optional<Formals> getFormals() const
    {
        if (hasFormals)
            return Formals{{formalsStart, nFormals}, ellipsis};
        else
            return std::nullopt;
    }

    Expr * body;
    DocComment docComment;

    ExprLambda(
        const PosTable & positions,
        std::pmr::polymorphic_allocator<char> & alloc,
        PosIdx pos,
        Symbol arg,
        const FormalsBuilder & formals,
        Expr * body)
        : pos(pos)
        , arg(arg)
        , hasFormals(true)
        , ellipsis(formals.ellipsis)
        , nFormals(formals.formals.size())
        , formalsStart(alloc.allocate_object<Formal>(nFormals))
        , body(body)
    {
        if (formals.formals.size() > nFormals) [[unlikely]] {
            auto err = Error(
                "too many formal arguments, implementation supports at most %1%",
                std::numeric_limits<decltype(nFormals)>::max());
            if (pos)
                err.atPos(positions[pos]);
            throw err;
        }
        std::uninitialized_copy_n(formals.formals.begin(), nFormals, formalsStart);
    };

    ExprLambda(PosIdx pos, Symbol arg, Expr * body)
        : pos(pos)
        , arg(arg)
        , hasFormals(false)
        , ellipsis(false)
        , nFormals(0)
        , formalsStart(nullptr)
        , body(body) {};

    ExprLambda(
        const PosTable & positions,
        std::pmr::polymorphic_allocator<char> & alloc,
        PosIdx pos,
        const FormalsBuilder & formals,
        Expr * body)
        : ExprLambda(positions, alloc, pos, Symbol(), formals, body) {};

    void setName(Symbol name) override;
    std::string showNamePos(const EvalState & state) const;

    PosIdx getPos() const override
    {
        return pos;
    }

    virtual void setDocComment(DocComment docComment) override;
    COMMON_METHODS
};

struct ExprCall : Expr
{
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
    }

    ExprCall(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args, PosIdx && cursedOrEndPos)
        : fun(fun)
        , args(args)
        , pos(pos)
        , cursedOrEndPos(cursedOrEndPos)
    {
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
    ExprAttrs * attrs;
    Expr * body;
    ExprLet(ExprAttrs * attrs, Expr * body)
        : attrs(attrs)
        , body(body) {};
    COMMON_METHODS
};

struct ExprWith : Expr
{
    PosIdx pos;
    uint32_t prevWith;
    Expr *attrs, *body;
    ExprWith * parentWith;
    ExprWith(const PosIdx & pos, Expr * attrs, Expr * body)
        : pos(pos)
        , attrs(attrs)
        , body(body) {};

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprIf : Expr
{
    PosIdx pos;
    Expr *cond, *then, *else_;
    ExprIf(const PosIdx & pos, Expr * cond, Expr * then, Expr * else_)
        : pos(pos)
        , cond(cond)
        , then(then)
        , else_(else_) {};

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprAssert : Expr
{
    PosIdx pos;
    Expr *cond, *body;
    ExprAssert(const PosIdx & pos, Expr * cond, Expr * body)
        : pos(pos)
        , cond(cond)
        , body(body) {};

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    Expr * e;
    ExprOpNot(Expr * e)
        : e(e) {};

    PosIdx getPos() const override
    {
        return e->getPos();
    }

    COMMON_METHODS
};

#define MakeBinOpMembers(name, s)                                                        \
    PosIdx pos;                                                                          \
    Expr *e1, *e2;                                                                       \
    name(Expr * e1, Expr * e2)                                                           \
        : e1(e1)                                                                         \
        , e2(e2){};                                                                      \
    name(const PosIdx & pos, Expr * e1, Expr * e2)                                       \
        : pos(pos)                                                                       \
        , e1(e1)                                                                         \
        , e2(e2){};                                                                      \
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
    PosIdx pos;
    bool forceString;
    std::span<std::pair<PosIdx, Expr *>> es;

    ExprConcatStrings(
        std::pmr::polymorphic_allocator<char> & alloc,
        const PosIdx & pos,
        bool forceString,
        std::span<std::pair<PosIdx, Expr *>> es)
        : pos(pos)
        , forceString(forceString)
        , es({alloc.allocate_object<std::pair<PosIdx, Expr *>>(es.size()), es.size()})
    {
        std::ranges::copy(es, this->es.begin());
    };

    ExprConcatStrings(
        std::pmr::polymorphic_allocator<char> & alloc,
        const PosIdx & pos,
        bool forceString,
        std::initializer_list<std::pair<PosIdx, Expr *>> es)
        : pos(pos)
        , forceString(forceString)
        , es({alloc.allocate_object<std::pair<PosIdx, Expr *>>(es.size()), es.size()})
    {
        std::ranges::copy(es, this->es.begin());
    };

    PosIdx getPos() const override
    {
        return pos;
    }

    COMMON_METHODS
};

struct ExprPos : Expr
{
    PosIdx pos;
    ExprPos(const PosIdx & pos)
        : pos(pos) {};

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

class Exprs
{
    std::pmr::monotonic_buffer_resource buffer;
public:
    std::pmr::polymorphic_allocator<char> alloc{&buffer};

    template<class C>
    [[gnu::always_inline]]
    C * add(auto &&... args)
    {
        return alloc.new_object<C>(std::forward<decltype(args)>(args)...);
    }

    // we define some calls to add explicitly so that the argument can be passed in as initializer lists
    template<class C>
    [[gnu::always_inline]]
    C * add(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args)
        requires(std::same_as<C, ExprCall>)
    {
        return alloc.new_object<C>(pos, fun, std::move(args));
    }

    template<class C>
    [[gnu::always_inline]]
    C * add(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args, PosIdx && cursedOrEndPos)
        requires(std::same_as<C, ExprCall>)
    {
        return alloc.new_object<C>(pos, fun, std::move(args), std::move(cursedOrEndPos));
    }

    template<class C>
    [[gnu::always_inline]]
    C *
    add(std::pmr::polymorphic_allocator<char> & alloc,
        const PosIdx & pos,
        bool forceString,
        std::span<std::pair<PosIdx, Expr *>> es)
        requires(std::same_as<C, ExprConcatStrings>)
    {
        return alloc.new_object<C>(alloc, pos, forceString, es);
    }

    template<class C>
    [[gnu::always_inline]]
    C *
    add(std::pmr::polymorphic_allocator<char> & alloc,
        const PosIdx & pos,
        bool forceString,
        std::initializer_list<std::pair<PosIdx, Expr *>> es)
        requires(std::same_as<C, ExprConcatStrings>)
    {
        return alloc.new_object<C>(alloc, pos, forceString, es);
    }
};

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
