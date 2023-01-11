#pragma once

#include <map>
#include <vector>

#include "value.hh"
#include "symbol-table.hh"
#include "error.hh"
#include "chunked-vector.hh"

namespace nix {


MakeError(EvalError, Error);
MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, Error);
MakeError(MissingArgumentError, EvalError);

/* Position objects. */
struct Pos
{
    uint32_t line;
    uint32_t column;

    struct none_tag { };
    struct Stdin { ref<std::string> source; };
    struct String { ref<std::string> source; };

    typedef std::variant<none_tag, Stdin, String, SourcePath> Origin;

    Origin origin;

    explicit operator bool() const { return line > 0; }

    operator std::shared_ptr<AbstractPos>() const;
};

class PosIdx {
    friend class PosTable;

private:
    uint32_t id;

    explicit PosIdx(uint32_t id): id(id) {}

public:
    PosIdx() : id(0) {}

    explicit operator bool() const { return id > 0; }

    bool operator <(const PosIdx other) const { return id < other.id; }

    bool operator ==(const PosIdx other) const { return id == other.id; }

    bool operator !=(const PosIdx other) const { return id != other.id; }
};

class PosTable
{
public:
    class Origin {
        friend PosTable;
    private:
        // must always be invalid by default, add() replaces this with the actual value.
        // subsequent add() calls use this index as a token to quickly check whether the
        // current origins.back() can be reused or not.
        mutable uint32_t idx = std::numeric_limits<uint32_t>::max();

        // Used for searching in PosTable::[].
        explicit Origin(uint32_t idx): idx(idx), origin{Pos::none_tag()} {}

    public:
        const Pos::Origin origin;

        Origin(Pos::Origin origin): origin(origin) {}
    };

    struct Offset {
        uint32_t line, column;
    };

private:
    std::vector<Origin> origins;
    ChunkedVector<Offset, 8192> offsets;

public:
    PosTable(): offsets(1024)
    {
        origins.reserve(1024);
    }

    PosIdx add(const Origin & origin, uint32_t line, uint32_t column)
    {
        const auto idx = offsets.add({line, column}).second;
        if (origins.empty() || origins.back().idx != origin.idx) {
            origin.idx = idx;
            origins.push_back(origin);
        }
        return PosIdx(idx + 1);
    }

    Pos operator[](PosIdx p) const
    {
        if (p.id == 0 || p.id > offsets.size())
            return {};
        const auto idx = p.id - 1;
        /* we want the last key <= idx, so we'll take prev(first key > idx).
           this is guaranteed to never rewind origin.begin because the first
           key is always 0. */
        const auto pastOrigin = std::upper_bound(
            origins.begin(), origins.end(), Origin(idx),
            [] (const auto & a, const auto & b) { return a.idx < b.idx; });
        const auto origin = *std::prev(pastOrigin);
        const auto offset = offsets[idx];
        return {offset.line, offset.column, origin.origin};
    }
};

inline PosIdx noPos = {};

std::ostream & operator << (std::ostream & str, const Pos & pos);


struct Env;
struct Value;
class EvalState;
struct StaticEnv;


/* An attribute path is a sequence of attribute names. */
struct AttrName
{
    Symbol symbol;
    Expr * expr;
    AttrName(Symbol s) : symbol(s) {};
    AttrName(Expr * e) : expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
    virtual ~Expr() { };
    virtual void show(const SymbolTable & symbols, std::ostream & str) const;
    virtual void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol name);
    virtual PosIdx getPos() const { return noPos; }
};

#define COMMON_METHODS \
    void show(const SymbolTable & symbols, std::ostream & str) const override; \
    void eval(EvalState & state, Env & env, Value & v) override; \
    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override;

struct ExprInt : Expr
{
    NixInt n;
    Value v;
    ExprInt(NixInt n) : n(n) { v.mkInt(n); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprFloat : Expr
{
    NixFloat nf;
    Value v;
    ExprFloat(NixFloat nf) : nf(nf) { v.mkFloat(nf); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprString : Expr
{
    std::string s;
    Value v;
    ExprString(std::string s) : s(std::move(s)) { v.mkString(this->s.data()); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprPath : Expr
{
    const SourcePath path;
    Value v;
    ExprPath(SourcePath && _path)
        : path(_path)
    {
        v.mkPath(&*path.accessor, path.path.abs().data());
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
       or function argument) or from a "with". */
    bool fromWith;

    /* In the former case, the value is obtained by going `level'
       levels up from the current environment and getting the
       `displ'th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name' from
       the set stored in the environment that is `level' levels up
       from the current one.*/
    Level level;
    Displacement displ;

    ExprVar(Symbol name) : name(name) { };
    ExprVar(const PosIdx & pos, Symbol name) : pos(pos), name(name) { };
    Value * maybeThunk(EvalState & state, Env & env) override;
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprSelect : Expr
{
    PosIdx pos;
    Expr * e, * def;
    AttrPath attrPath;
    ExprSelect(const PosIdx & pos, Expr * e, const AttrPath & attrPath, Expr * def) : pos(pos), e(e), def(def), attrPath(attrPath) { };
    ExprSelect(const PosIdx & pos, Expr * e, Symbol name) : pos(pos), e(e), def(0) { attrPath.push_back(AttrName(name)); };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    AttrPath attrPath;
    ExprOpHasAttr(Expr * e, const AttrPath & attrPath) : e(e), attrPath(attrPath) { };
    PosIdx getPos() const override { return e->getPos(); }
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    PosIdx pos;
    struct AttrDef {
        bool inherited;
        Expr * e;
        PosIdx pos;
        Displacement displ; // displacement
        AttrDef(Expr * e, const PosIdx & pos, bool inherited=false)
            : inherited(inherited), e(e), pos(pos) { };
        AttrDef() { };
    };
    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    struct DynamicAttrDef {
        Expr * nameExpr, * valueExpr;
        PosIdx pos;
        DynamicAttrDef(Expr * nameExpr, Expr * valueExpr, const PosIdx & pos)
            : nameExpr(nameExpr), valueExpr(valueExpr), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;
    ExprAttrs(const PosIdx &pos) : recursive(false), pos(pos) { };
    ExprAttrs() : recursive(false) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprList : Expr
{
    std::vector<Expr *> elems;
    ExprList() { };
    COMMON_METHODS

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
    Formals_ formals;
    bool ellipsis;

    bool has(Symbol arg) const
    {
        auto it = std::lower_bound(formals.begin(), formals.end(), arg,
            [] (const Formal & f, const Symbol & sym) { return f.name < sym; });
        return it != formals.end() && it->name == arg;
    }

    std::vector<Formal> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<Formal> result(formals.begin(), formals.end());
        std::sort(result.begin(), result.end(),
            [&] (const Formal & a, const Formal & b) {
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
    Formals * formals;
    Expr * body;
    ExprLambda(PosIdx pos, Symbol arg, Formals * formals, Expr * body)
        : pos(pos), arg(arg), formals(formals), body(body)
    {
    };
    ExprLambda(PosIdx pos, Formals * formals, Expr * body)
        : pos(pos), formals(formals), body(body)
    {
    }
    void setName(Symbol name) override;
    std::string showNamePos(const EvalState & state) const;
    inline bool hasFormals() const { return formals != nullptr; }
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprCall : Expr
{
    Expr * fun;
    std::vector<Expr *> args;
    PosIdx pos;
    ExprCall(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args)
        : fun(fun), args(args), pos(pos)
    { }
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprLet : Expr
{
    ExprAttrs * attrs;
    Expr * body;
    ExprLet(ExprAttrs * attrs, Expr * body) : attrs(attrs), body(body) { };
    COMMON_METHODS
};

struct ExprWith : Expr
{
    PosIdx pos;
    Expr * attrs, * body;
    size_t prevWith;
    ExprWith(const PosIdx & pos, Expr * attrs, Expr * body) : pos(pos), attrs(attrs), body(body) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprIf : Expr
{
    PosIdx pos;
    Expr * cond, * then, * else_;
    ExprIf(const PosIdx & pos, Expr * cond, Expr * then, Expr * else_) : pos(pos), cond(cond), then(then), else_(else_) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprAssert : Expr
{
    PosIdx pos;
    Expr * cond, * body;
    ExprAssert(const PosIdx & pos, Expr * cond, Expr * body) : pos(pos), cond(cond), body(body) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    Expr * e;
    ExprOpNot(Expr * e) : e(e) { };
    COMMON_METHODS
};

#define MakeBinOp(name, s) \
    struct name : Expr \
    { \
        PosIdx pos; \
        Expr * e1, * e2; \
        name(Expr * e1, Expr * e2) : e1(e1), e2(e2) { }; \
        name(const PosIdx & pos, Expr * e1, Expr * e2) : pos(pos), e1(e1), e2(e2) { }; \
        void show(const SymbolTable & symbols, std::ostream & str) const override \
        { \
            str << "("; e1->show(symbols, str); str << " " s " "; e2->show(symbols, str); str << ")"; \
        } \
        void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override \
        { \
            e1->bindVars(es, env); e2->bindVars(es, env);    \
        } \
        void eval(EvalState & state, Env & env, Value & v) override; \
        PosIdx getPos() const override { return pos; } \
    };

MakeBinOp(ExprOpEq, "==")
MakeBinOp(ExprOpNEq, "!=")
MakeBinOp(ExprOpAnd, "&&")
MakeBinOp(ExprOpOr, "||")
MakeBinOp(ExprOpImpl, "->")
MakeBinOp(ExprOpUpdate, "//")
MakeBinOp(ExprOpConcatLists, "++")

struct ExprConcatStrings : Expr
{
    PosIdx pos;
    bool forceString;
    std::vector<std::pair<PosIdx, Expr *>> * es;
    ExprConcatStrings(const PosIdx & pos, bool forceString, std::vector<std::pair<PosIdx, Expr *>> * es)
        : pos(pos), forceString(forceString), es(es) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprPos : Expr
{
    PosIdx pos;
    ExprPos(const PosIdx & pos) : pos(pos) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    bool isWith;
    const StaticEnv * up;

    // Note: these must be in sorted order.
    typedef std::vector<std::pair<Symbol, Displacement>> Vars;
    Vars vars;

    StaticEnv(bool isWith, const StaticEnv * up, size_t expectedSize = 0) : isWith(isWith), up(up) {
        vars.reserve(expectedSize);
    };

    void sort()
    {
        std::stable_sort(vars.begin(), vars.end(),
            [](const Vars::value_type & a, const Vars::value_type & b) { return a.first < b.first; });
    }

    void deduplicate()
    {
        auto it = vars.begin(), jt = it, end = vars.end();
        while (jt != end) {
            *it = *jt++;
            while (jt != end && it->first == jt->first) *it = *jt++;
            it++;
        }
        vars.erase(it, end);
    }

    Vars::const_iterator find(Symbol name) const
    {
        Vars::value_type key(name, 0);
        auto i = std::lower_bound(vars.begin(), vars.end(), key);
        if (i != vars.end() && i->first == name) return i;
        return vars.end();
    }
};


}
