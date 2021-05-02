#pragma once

#include "value.hh"
#include "symbol-table.hh"
#include "error.hh"

#include <map>


namespace nix {


MakeError(EvalError, Error);
MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, Error);
MakeError(MissingArgumentError, EvalError);
MakeError(RestrictedPathError, Error);


/* Position objects. */

struct Pos
{
    FileOrigin origin;
    Symbol file;
    unsigned int line, column;
    Pos() : origin(foString), line(0), column(0) { };
    Pos(FileOrigin origin, const Symbol & file, unsigned int line, unsigned int column)
        : origin(origin), file(file), line(line), column(column) { };
    operator bool() const
    {
        return line != 0;
    }
    bool operator < (const Pos & p2) const
    {
        if (!line) return p2.line;
        if (!p2.line) return false;
        int d = ((string) file).compare((string) p2.file);
        if (d < 0) return true;
        if (d > 0) return false;
        if (line < p2.line) return true;
        if (line > p2.line) return false;
        return column < p2.column;
    }
};

extern Pos noPos;

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
    AttrName(const Symbol & s) : symbol(s) {};
    AttrName(Expr * e) : expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

string showAttrPath(const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
    virtual ~Expr() { };
    virtual void show(std::ostream & str) const;
    virtual void showAsJson(std::ostream & str) const;
    virtual void showAsJsonArrays(std::ostream & str) const;
    virtual void showAsJsonArraysFmt(std::ostream & str) const;
    virtual void showAsJsonNumtypes(std::ostream & str) const;
    //virtual void showAsXml(std::ostream & str) const;
    virtual void bindVars(const StaticEnv & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol & name);
};

std::ostream & operator << (std::ostream & str, const Expr & e);

//void showAsXml(std::ostream & str) const;
#define COMMON_METHODS \
    void show(std::ostream & str) const; \
    void showAsJson(std::ostream & str) const; \
    void showAsJsonArrays(std::ostream & str) const; \
    void showAsJsonArraysFmt(std::ostream & str) const; \
    void showAsJsonNumtypes(std::ostream & str) const; \
    void eval(EvalState & state, Env & env, Value & v); \
    void bindVars(const StaticEnv & env);

struct ExprInt : Expr
{
    NixInt n;
    Value v;
    ExprInt(NixInt n) : n(n) { mkInt(v, n); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprFloat : Expr
{
    NixFloat nf;
    Value v;
    ExprFloat(NixFloat nf) : nf(nf) { mkFloat(v, nf); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprString : Expr
{
    Symbol s;
    Value v;
    ExprString(const Symbol & s) : s(s) { mkString(v, s); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

/* Temporary class used during parsing of indented strings. */
struct ExprIndStr : Expr
{
    string s;
    ExprIndStr(const string & s) : s(s) { };
};

struct ExprPath : Expr
{
    string s;
    Value v;
    ExprPath(const string & s) : s(s) { v.mkPath(this->s.c_str()); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprVar : Expr
{
    Pos pos;
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
    unsigned int level;
    unsigned int displ;

    ExprVar(const Symbol & name) : name(name) { };
    ExprVar(const Pos & pos, const Symbol & name) : pos(pos), name(name) { };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprSelect : Expr
{
    Pos pos;
    Expr * e, * def;
    AttrPath attrPath;
    ExprSelect(const Pos & pos, Expr * e, const AttrPath & attrPath, Expr * def) : pos(pos), e(e), def(def), attrPath(attrPath) { };
    ExprSelect(const Pos & pos, Expr * e, const Symbol & name) : pos(pos), e(e), def(0) { attrPath.push_back(AttrName(name)); };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    AttrPath attrPath;
    ExprOpHasAttr(Expr * e, const AttrPath & attrPath) : e(e), attrPath(attrPath) { };
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    struct AttrDef {
        bool inherited;
        Expr * e;
        Pos pos;
        unsigned int displ; // displacement
        AttrDef(Expr * e, const Pos & pos, bool inherited=false)
            : inherited(inherited), e(e), pos(pos) { };
        AttrDef() { };
    };
    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    struct DynamicAttrDef {
        Expr * nameExpr, * valueExpr;
        Pos pos;
        DynamicAttrDef(Expr * nameExpr, Expr * valueExpr, const Pos & pos)
            : nameExpr(nameExpr), valueExpr(valueExpr), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;
    ExprAttrs() : recursive(false) { };
    COMMON_METHODS
};

struct ExprList : Expr
{
    std::vector<Expr *> elems;
    ExprList() { };
    COMMON_METHODS
};

struct Formal
{
    Pos pos;
    Symbol name;
    Expr * def;
    Formal(const Pos & pos, const Symbol & name, Expr * def) : pos(pos), name(name), def(def) { };
};

struct Formals
{
    typedef std::list<Formal> Formals_;
    Formals_ formals;
    std::set<Symbol> argNames; // used during parsing
    bool ellipsis;
};

struct ExprLambda : Expr
{
    Pos pos;
    Symbol name;
    Symbol arg;
    bool matchAttrs;
    Formals * formals;
    Expr * body;
    ExprLambda(const Pos & pos, const Symbol & arg, bool matchAttrs, Formals * formals, Expr * body)
        : pos(pos), arg(arg), matchAttrs(matchAttrs), formals(formals), body(body)
    {
        if (!arg.empty() && formals && formals->argNames.find(arg) != formals->argNames.end())
            throw ParseError({
                .msg = hintfmt("duplicate formal function argument '%1%'", arg),
                .errPos = pos
            });
    };
    void setName(Symbol & name);
    string showNamePos() const;
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
    Pos pos;
    Expr * attrs, * body;
    size_t prevWith;
    ExprWith(const Pos & pos, Expr * attrs, Expr * body) : pos(pos), attrs(attrs), body(body) { };
    COMMON_METHODS
};

struct ExprIf : Expr
{
    Pos pos;
    Expr * cond, * then, * else_;
    ExprIf(const Pos & pos, Expr * cond, Expr * then, Expr * else_) : pos(pos), cond(cond), then(then), else_(else_) { };
    COMMON_METHODS
};

struct ExprAssert : Expr
{
    Pos pos;
    Expr * cond, * body;
    ExprAssert(const Pos & pos, Expr * cond, Expr * body) : pos(pos), cond(cond), body(body) { };
    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    Expr * e;
    ExprOpNot(Expr * e) : e(e) { };
    COMMON_METHODS
};



// define NodeTypeId::ExprLambda etc.
// similar: value.hh: typedef enum { ... } InternalType;
enum class NodeTypeId {
    NotUsed = 0, // make IDs one-based -> sync with line numbers in *.def file
    // https://stackoverflow.com/a/320888/10440128
#   define ADD_TYPE(t) t,
#   include "nixexpr-node-types.def"
#   undef ADD_TYPE
    NodeTypeCount
};

// define NodeTypeName::ExprLambda etc.
struct NodeTypeName {
#   define ADD_TYPE(t) static std::string t;
#   include "nixexpr-node-types.def"
#   undef ADD_TYPE
    // values are defined in nixexpr.cc
};

/*
// define NodeTypeNameOfId[NodeTypeId::ExprLambda] etc.
char const* const NodeTypeNameOfId[] = {
    "NotUsed",
#   define ADD_TYPE(t) #t,
#   include "nixexpr-node-types.def"
#   undef ADD_TYPE
    0
};
*/



// https://nixos.wiki/wiki/Nix_Expression_Language#Types
// https://nixos.org/manual/nix/stable/#ssec-values
// note. we use `enum class` to keep the enum-items local





// jsonTypeName sample: name ExprOpAnd -> jsonTypeName opAnd
// TODO showAsJson: fix recursion: call e1->showAsJson(str), etc.
/*
            std::string jsonTypeName = [] { \
                std::string res = ((std::string) #name).substr(4); \
                if (res.length() > 0 && 'A' <= res[0] && res[0] <= 'Z') \
                    res[0] += 32; \
                return res; \
            }(); \
            \
            str << "{\"type\":\"" << jsonTypeName << "\"";   \
*/
#define MakeBinOp(name, s) \
    struct name : Expr \
    { \
        Pos pos; \
        Expr * e1, * e2; \
        name(Expr * e1, Expr * e2) : e1(e1), e2(e2) { }; \
        name(const Pos & pos, Expr * e1, Expr * e2) : pos(pos), e1(e1), e2(e2) { }; \
        void show(std::ostream & str) const \
        { \
            str << "(" << *e1 << " " s " " << *e2 << ")";   \
        } \
        void showAsJson(std::ostream & str) const \
        { \
            str << "{\"type\":\"" << NodeTypeName::name << "\"";   \
            str << ",\"op1\":"; e1->showAsJson(str);   \
            str << ",\"op2\":"; e2->showAsJson(str);   \
            str << "}";   \
        } \
        void showAsJsonNumtypes(std::ostream & str) const \
        { \
            str << "{\"type\":" << (int) NodeTypeId::name;   \
            str << ",\"op1\":"; e1->showAsJsonNumtypes(str);   \
            str << ",\"op2\":"; e2->showAsJsonNumtypes(str);   \
            str << "}";   \
        } \
        void showAsJsonArrays(std::ostream & str) const \
        { \
            str << '[' << (int) NodeTypeId::name;   \
            str << ','; e1->showAsJsonArrays(str);   \
            str << ','; e2->showAsJsonArrays(str);   \
            str << ']';   \
        } \
        void showAsJsonArraysFmt(std::ostream & str) const \
        { \
            str << '[' << (int) NodeTypeId::name;   \
            str << ','; e1->showAsJsonArraysFmt(str);   \
            str << ','; e2->showAsJsonArraysFmt(str);   \
            str << ']';   \
        } \
        void bindVars(const StaticEnv & env) \
        { \
            e1->bindVars(env); e2->bindVars(env); \
        } \
        void eval(EvalState & state, Env & env, Value & v); \
    };

MakeBinOp(ExprApp, "")
MakeBinOp(ExprOpEq, "==")
MakeBinOp(ExprOpNEq, "!=")
MakeBinOp(ExprOpAnd, "&&")
MakeBinOp(ExprOpOr, "||")
MakeBinOp(ExprOpImpl, "->")
MakeBinOp(ExprOpUpdate, "//")
MakeBinOp(ExprOpConcatLists, "++")

struct ExprConcatStrings : Expr
{
    Pos pos;
    bool forceString;
    vector<Expr *> * es;
    ExprConcatStrings(const Pos & pos, bool forceString, vector<Expr *> * es)
        : pos(pos), forceString(forceString), es(es) { };
    COMMON_METHODS
};

// TODO what is Pos?
struct ExprPos : Expr
{
    Pos pos;
    ExprPos(const Pos & pos) : pos(pos) { };
    COMMON_METHODS
};


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    bool isWith;
    const StaticEnv * up;
    typedef std::map<Symbol, unsigned int> Vars;
    Vars vars;
    StaticEnv(bool isWith, const StaticEnv * up) : isWith(isWith), up(up) { };
};

void AttrPath_showAsJson(std::ostream & out, const AttrPath & attrPath);
void AttrPath_showAsJsonArrays(std::ostream & out, const AttrPath & attrPath);
void AttrPath_showAsJsonArraysFmt(std::ostream & out, const AttrPath & attrPath);
void AttrPath_showAsJsonNumtypes(std::ostream & out, const AttrPath & attrPath);

}
