#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {


/* Displaying abstract syntax trees. */

std::ostream & operator << (std::ostream & str, const Expr & e)
{
    e.show(str);
    return str;
}

// TODO use std::string
static void showString(std::ostream & str, const string & s)
{
    str << '"';
    for (auto c : (string) s)
        if (c == '"' || c == '\\' || c == '$') str << "\\" << c;
        else if (c == '\n') str << "\\n";
        else if (c == '\r') str << "\\r";
        else if (c == '\t') str << "\\t";
        else str << c;
    str << '"';
}

// TODO use std::string
static void showId(std::ostream & str, const string & s)
{
    // FIXME: use s.append() or std::ostringstream
    if (s.empty())
        str << "\"\"";
    // TODO use if (s.compare("if") == 0)
    else if (s == "if") // FIXME: handle other keywords
        str << '"' << s << '"';
    else {
        char c = s[0];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
            showString(str, s);
            return;
        }
        for (auto c : s)
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '\'' || c == '-')) {
                showString(str, s);
                return;
            }
        str << s;
    }
}

std::ostream & operator << (std::ostream & str, const Symbol & sym)
{
    showId(str, *sym.s);
    return str;
}



// format switch

void Expr::show(std::ostream & str) const
{
    this->showAsAterm(str); // default format
}

void ExprAsAterm::show(std::ostream & str) const
{
    this->showAsAterm(str);
}

void ExprAsJson::show(std::ostream & str) const
{
    this->showAsJson(str);
}



// aterm output format

void Expr::showAsAterm(std::ostream & str) const
{
    abort();
}

void ExprInt::showAsAterm(std::ostream & str) const
{
    str << n;
}

void ExprFloat::showAsAterm(std::ostream & str) const
{
    str << nf;
}

void ExprString::showAsAterm(std::ostream & str) const
{
    showString(str, s);
}

void ExprPath::showAsAterm(std::ostream & str) const
{
    str << s;
}

void ExprVar::showAsAterm(std::ostream & str) const
{
    str << name;
}

void ExprSelect::showAsAterm(std::ostream & str) const
{
    str << "(" << *e << ")." << showAttrPath(attrPath);
    if (def) str << " or (" << *def << ")";
}

void ExprOpHasAttr::showAsAterm(std::ostream & str) const
{
    str << "((" << *e << ") ? " << showAttrPath(attrPath) << ")";
}

void ExprAttrs::showAsAterm(std::ostream & str) const
{
    if (recursive) str << "rec ";
    str << "{ ";
    for (auto & i : attrs)
        if (i.second.inherited)
            str << "inherit " << i.first << " " << "; ";
        else
            str << i.first << " = " << *i.second.e << "; ";
    for (auto & i : dynamicAttrs)
        str << "\"${" << *i.nameExpr << "}\" = " << *i.valueExpr << "; ";
    str << "}";
}

void ExprList::showAsAterm(std::ostream & str) const
{
    str << "[ ";
    for (auto & i : elems)
        str << "(" << *i << ") ";
    str << "]";
}

void ExprLambda::showAsAterm(std::ostream & str) const
{
    str << "(";
    if (hasFormals()) {
        str << "{ ";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ", ";
            str << i.name;
            if (i.def) str << " ? " << *i.def;
        }
        if (formals->ellipsis) {
            if (!first) str << ", ";
            str << "...";
        }
        str << " }";
        if (!arg.empty()) str << " @ ";
    }
    if (!arg.empty()) str << arg;
    str << ": " << *body << ")";
}

void ExprCall::showAsAterm(std::ostream & str) const
{
    str << '(' << *fun;
    for (auto e : args) {
        str <<  ' ';
        str << *e;
    }
    str << ')';
}

void ExprLet::showAsAterm(std::ostream & str) const
{
    str << "(let ";
    for (auto & i : attrs->attrs)
        if (i.second.inherited) {
            str << "inherit " << i.first << "; ";
        }
        else
            str << i.first << " = " << *i.second.e << "; ";
    str << "in " << *body << ")";
}

void ExprWith::showAsAterm(std::ostream & str) const
{
    str << "(with " << *attrs << "; " << *body << ")";
}

void ExprIf::showAsAterm(std::ostream & str) const
{
    str << "(if " << *cond << " then " << *then << " else " << *else_ << ")";
}

void ExprAssert::showAsAterm(std::ostream & str) const
{
    str << "assert " << *cond << "; " << *body;
}

void ExprOpNot::showAsAterm(std::ostream & str) const
{
    str << "(! " << *e << ")";
}

void ExprConcatStrings::showAsAterm(std::ostream & str) const
{
    bool first = true;
    str << "(";
    for (auto & i : *es) {
        if (first) first = false; else str << " + ";
        str << *i;
    }
    str << ")";
}

void ExprPos::showAsAterm(std::ostream & str) const
{
    str << "__curPos";
}



// json output format

void ExprInt::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"int\"";
    str << ",\"value\":" << n << "";
    str << "}";
}

void ExprFloat::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"float\"";
    str << ",\"value\":" << nf << "";
    str << "}";
}

void ExprString::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"string\"";
    str << ",\"value\":\"" << s << "\""; // TODO escape string for json -> showString(str, s);
    str << "}";
}

void ExprPath::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"path\"";
    str << ",\"value\":\"" << s << "\""; // TODO escape string for json?
    str << "}";
}

void ExprVar::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"var\"";
    str << ",\"name\":\"" << name << "\"";
    str << "}";
}

void ExprSelect::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"select\"";
    str << ",\"set\":" << *e << "";
    str << ",\"attr\":\"" << showAttrPath(attrPath) << "\""; // TODO escape attrPath for json?
    if (def)
    str << ",\"default\":" << *def << "";
    str << " }"
}

void ExprOpHasAttr::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"opHasAttr\"";
    str << ",\"op\":" << *e << "";
    str << ",\"attr\":\"" << showAttrPath(attrPath) << "\""; // TODO escape attrPath for json?
    str << "}";
}

void ExprAttrs::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"attrs\"";
    str << ",\"recursive\":" << (recursive ? "true" : "false") << "";
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs)
    {
        if (first) first = false; else str << ",";
        str << "{\"type\":\"attr\"";
        str << ",\"inherited\":" << (i.second.inherited ? "true" : "false") << "";
        if (i.second.inherited)
            str << ",\"name\":\"" << i.first << "\""; // TODO format name for json?
        else
            str << ",\"name\":\"" << i.first << "\""; // TODO format name for json?
            str << ",\"value\":" << *i.second.e;
        str << "}";
    }
    str << "]}";
    str << ",\"dynamicAttrs\":[";
    first = true;
    for (auto & i : dynamicAttrs)
    {
        if (first) first = false; else str << ",";
        str << "{\"type\":\"attr\"";
        str << ",\"nameExpr\":\"" << *i.nameExpr << "\""; // TODO format expr for json?
        str << ",\"valueExpr\":" << *i.valueExpr; // TODO format expr for json?
        str << "}";
    }
    str << "]}";
}

void ExprList::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"list\"";
    str << ",\"items\":["; // TODO name? items, elements, values
    bool first = true;
    for (auto & i : elems)
        if (first) first = false; else str << ",";
        str << *i; // TODO format expr for json?
    str << "]}";
}

void ExprLambda::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"lambda\"";
    str << ",\"matchAttrs\":" << (matchAttrs ? "true" : "false");
    if (matchAttrs) {
        str << ",\"formals\":[";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ",";
            str << "{\"type\":\"formal\"";
            str << ",\"name\":\"" << i.name << "\"";
            if (i.def)
            str << ",\"default\":" << *i.def;
            str << "}";
        }
        str << "]";
        str << ",\"ellipsis\":" << (formals->ellipsis ? "true" : "false");
    }
    if (!arg.empty())
    str << ",\"arg\":" << arg;
    str << ",\"body\":" << *body;
    str << "}";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#let_..._in_statement
void ExprLet::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"let\"";
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs)
        if (first) first = false; else str << ",";
        str << "{\"type\":\"attr\"";
        str << ",\"inherited\":" << (i.second.inherited ? "true" : "false") << "";
        if (i.second.inherited)
            str << "\"name\":\"" << i.first << "\""; // TODO escape name for json?
        else
            str << "\"name\":\"" << i.first << "\", \"value\":" << *i.second.e; // TODO escape name for json?
        str << "}";
    str << "]}";
    str << ",\"body\":" << *body;
    str << "}";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#with_statement
void ExprWith::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"with\"";
    str << ",\"set\":" << *attrs;
    str << ",\"body\":" << *body;
    str << "}";
}

void ExprIf::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"if\"";
    str << ",\"cond\":" << *cond;
    str << ",\"then\":" << *then;
    str << ",\"else\":" << *else_;
    str << "}";
}

void ExprAssert::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"assert\"";
    str << ",\"cond\":" << *cond;
    str << ",\"body\":" << *body;
    str << "}";
}

void ExprOpNot::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"opNot\"";
    str << ",\"expr\":" << *e;
    str << "}";
}

void ExprConcatStrings::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"concatStrings\"";
    str << ",\"strings\":[";
    bool first = true;
    for (auto & i : attrs)
        if (first) first = false; else str << ",";
        str << *i;
    str << "]}";
}

void ExprPos::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"pos\"}";
}



std::ostream & operator << (std::ostream & str, const Pos & pos)
{
    if (!pos)
        str << "undefined position";
    else
    {
        auto f = format(ANSI_BOLD "%1%" ANSI_NORMAL ":%2%:%3%");
        switch (pos.origin) {
            case foFile:
                f % (string) pos.file;
                break;
            case foStdin:
            case foString:
                f % "(string)";
                break;
            default:
                throw Error("unhandled Pos origin!");
        }
        str << (f % pos.line % pos.column).str();
    }

    return str;
}


string showAttrPath(const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << '.'; else first = false;
        if (i.symbol.set())
            out << i.symbol;
        else
            out << "\"${" << *i.expr << "}\"";
    }
    return out.str();
}


Pos noPos;


/* Computing levels/displacements for variables. */

void Expr::bindVars(const StaticEnv & env)
{
    abort();
}

void ExprInt::bindVars(const StaticEnv & env)
{
}

void ExprFloat::bindVars(const StaticEnv & env)
{
}

void ExprString::bindVars(const StaticEnv & env)
{
}

void ExprPath::bindVars(const StaticEnv & env)
{
}

void ExprVar::bindVars(const StaticEnv & env)
{
    /* Check whether the variable appears in the environment.  If so,
       set its level and displacement. */
    const StaticEnv * curEnv;
    Level level;
    int withLevel = -1;
    for (curEnv = &env, level = 0; curEnv; curEnv = curEnv->up, level++) {
        if (curEnv->isWith) {
            if (withLevel == -1) withLevel = level;
        } else {
            auto i = curEnv->find(name);
            if (i != curEnv->vars.end()) {
                fromWith = false;
                this->level = level;
                displ = i->second;
                return;
            }
        }
    }

    /* Otherwise, the variable must be obtained from the nearest
       enclosing `with'.  If there is no `with', then we can issue an
       "undefined variable" error now. */
    if (withLevel == -1)
        throw UndefinedVarError({
            .msg = hintfmt("undefined variable '%1%'", name),
            .errPos = pos
        });
    fromWith = true;
    this->level = withLevel;
}

void ExprSelect::bindVars(const StaticEnv & env)
{
    e->bindVars(env);
    if (def) def->bindVars(env);
    for (auto & i : attrPath)
        if (!i.symbol.set())
            i.expr->bindVars(env);
}

void ExprOpHasAttr::bindVars(const StaticEnv & env)
{
    e->bindVars(env);
    for (auto & i : attrPath)
        if (!i.symbol.set())
            i.expr->bindVars(env);
}

void ExprAttrs::bindVars(const StaticEnv & env)
{
    const StaticEnv * dynamicEnv = &env;
    StaticEnv newEnv(false, &env, recursive ? attrs.size() : 0);

    if (recursive) {
        dynamicEnv = &newEnv;

        Displacement displ = 0;
        for (auto & i : attrs)
            newEnv.vars.emplace_back(i.first, i.second.displ = displ++);

        // No need to sort newEnv since attrs is in sorted order.

        for (auto & i : attrs)
            i.second.e->bindVars(i.second.inherited ? env : newEnv);
    }

    else
        for (auto & i : attrs)
            i.second.e->bindVars(env);

    for (auto & i : dynamicAttrs) {
        i.nameExpr->bindVars(*dynamicEnv);
        i.valueExpr->bindVars(*dynamicEnv);
    }
}

void ExprList::bindVars(const StaticEnv & env)
{
    for (auto & i : elems)
        i->bindVars(env);
}

void ExprLambda::bindVars(const StaticEnv & env)
{
    StaticEnv newEnv(
        false, &env,
        (hasFormals() ? formals->formals.size() : 0) +
        (arg.empty() ? 0 : 1));

    Displacement displ = 0;

    if (!arg.empty()) newEnv.vars.emplace_back(arg, displ++);

    if (hasFormals()) {
        for (auto & i : formals->formals)
            newEnv.vars.emplace_back(i.name, displ++);

        newEnv.sort();

        for (auto & i : formals->formals)
            if (i.def) i.def->bindVars(newEnv);
    }

    body->bindVars(newEnv);
}

void ExprLet::bindVars(const StaticEnv & env)
{
    StaticEnv newEnv(false, &env, attrs->attrs.size());

    Displacement displ = 0;
    for (auto & i : attrs->attrs)
        newEnv.vars.emplace_back(i.first, i.second.displ = displ++);

    // No need to sort newEnv since attrs->attrs is in sorted order.

    for (auto & i : attrs->attrs)
        i.second.e->bindVars(i.second.inherited ? env : newEnv);

    body->bindVars(newEnv);
}

void ExprWith::bindVars(const StaticEnv & env)
{
    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    Level level;
    prevWith = 0;
    for (curEnv = &env, level = 1; curEnv; curEnv = curEnv->up, level++)
        if (curEnv->isWith) {
            prevWith = level;
            break;
        }

    attrs->bindVars(env);
    StaticEnv newEnv(true, &env);
    body->bindVars(newEnv);
}

void ExprIf::bindVars(const StaticEnv & env)
{
    cond->bindVars(env);
    then->bindVars(env);
    else_->bindVars(env);
}

void ExprAssert::bindVars(const StaticEnv & env)
{
    cond->bindVars(env);
    body->bindVars(env);
}

void ExprOpNot::bindVars(const StaticEnv & env)
{
    e->bindVars(env);
}

void ExprConcatStrings::bindVars(const StaticEnv & env)
{
    for (auto & i : *es)
        i->bindVars(env);
}

void ExprPos::bindVars(const StaticEnv & env)
{
}


/* Storing function names. */

void Expr::setName(Symbol & name)
{
}


void ExprLambda::setName(Symbol & name)
{
    this->name = name;
    body->setName(name);
}


string ExprLambda::showNamePos() const
{
    return (format("%1% at %2%") % (name.set() ? "'" + (string) name + "'" : "anonymous function") % pos).str();
}



/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    for (auto & i : symbols)
        n += i.size();
    return n;
}


}
