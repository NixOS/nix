#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {


/* Displaying abstract syntax trees. */

std::ostream & operator << (std::ostream & str, Expr & e)
{
    e.show(str);
    return str;
}

void Expr::show(std::ostream & str)
{
    abort();
}

void ExprInt::show(std::ostream & str)
{
    str << n;
}

void ExprString::show(std::ostream & str)
{
    str << "\"" << s << "\""; // !!! escaping
}

void ExprPath::show(std::ostream & str)
{
    str << s;
}

void ExprVar::show(std::ostream & str)
{
    str << name;
}

void ExprSelect::show(std::ostream & str)
{
    str << "(" << *e << ")." << showAttrPath(attrPath);
    if (def) str << " or " << *def;
}

void ExprOpHasAttr::show(std::ostream & str)
{
    str << "(" << *e << ") ? " << showAttrPath(attrPath);
}

void ExprAttrs::show(std::ostream & str)
{
    if (recursive) str << "rec ";
    str << "{ ";
    foreach (AttrDefs::iterator, i, attrs)
        if (i->second.inherited)
            str << "inherit " << i->first << " " << "; ";
        else
            str << i->first << " = " << *i->second.e << "; ";
    foreach (DynamicAttrDefs::iterator, i, dynamicAttrs)
        str << "\"${" << *i->nameExpr << "}\" = " << *i->valueExpr << "; ";
    str << "}";
}

void ExprList::show(std::ostream & str)
{
    str << "[ ";
    foreach (vector<Expr *>::iterator, i, elems)
        str << "(" << **i << ") ";
    str << "]";
}

void ExprLambda::show(std::ostream & str)
{
    str << "(";
    if (matchAttrs) {
        str << "{ ";
        bool first = true;
        foreach (Formals::Formals_::iterator, i, formals->formals) {
            if (first) first = false; else str << ", ";
            str << i->name;
            if (i->def) str << " ? " << *i->def;
        }
        str << " }";
        if (!arg.empty()) str << " @ ";
    }
    if (!arg.empty()) str << arg;
    str << ": " << *body << ")";
}

void ExprLet::show(std::ostream & str)
{
    str << "let ";
    foreach (ExprAttrs::AttrDefs::iterator, i, attrs->attrs)
        if (i->second.inherited)
            str << "inherit " << i->first << "; ";
        else
            str << i->first << " = " << *i->second.e << "; ";
    str << "in " << *body;
}

void ExprWith::show(std::ostream & str)
{
    str << "with " << *attrs << "; " << *body;
}

void ExprIf::show(std::ostream & str)
{
    str << "if " << *cond << " then " << *then << " else " << *else_;
}

void ExprAssert::show(std::ostream & str)
{
    str << "assert " << *cond << "; " << *body;
}

void ExprOpNot::show(std::ostream & str)
{
    str << "! " << *e;
}

void ExprBuiltin::show(std::ostream & str)
{
    str << "builtins." << name;
}

void ExprConcatStrings::show(std::ostream & str)
{
    bool first = true;
    foreach (vector<Expr *>::iterator, i, *es) {
        if (first) first = false; else str << " + ";
        str << **i;
    }
}

void ExprPos::show(std::ostream & str)
{
    str << "__curPos";
}


std::ostream & operator << (std::ostream & str, const Pos & pos)
{
    if (!pos)
        str << "undefined position";
    else
        str << (format("%1%:%2%:%3%") % pos.file % pos.line % pos.column).str();
    return str;
}


string showAttrPath(const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    foreach (AttrPath::const_iterator, i, attrPath) {
        if (!first)
            out << '.';
        else
            first = false;
        if (i->symbol.set())
            out << i->symbol;
        else
            out << "\"${" << *i->expr << "}\"";
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
    unsigned int level;
    int withLevel = -1;
    for (curEnv = &env, level = 0; curEnv; curEnv = curEnv->up, level++) {
        if (curEnv->isWith) {
            if (withLevel == -1) withLevel = level;
        } else {
            StaticEnv::Vars::const_iterator i = curEnv->vars.find(name);
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
    if (withLevel == -1) throw UndefinedVarError(format("undefined variable `%1%' at %2%") % name % pos);

    fromWith = true;
    this->level = withLevel;
}

void ExprSelect::bindVars(const StaticEnv & env)
{
    e->bindVars(env);
    if (def) def->bindVars(env);
    foreach (AttrPath::iterator, i, attrPath)
        if (!i->symbol.set())
            i->expr->bindVars(env);
}

void ExprOpHasAttr::bindVars(const StaticEnv & env)
{
    e->bindVars(env);
    foreach (AttrPath::iterator, i, attrPath)
        if (!i->symbol.set())
            i->expr->bindVars(env);
}

void ExprAttrs::bindVars(const StaticEnv & env)
{
    const StaticEnv * dynamicEnv = &env;
    StaticEnv newEnv(false, &env);

    if (recursive) {
        dynamicEnv = &newEnv;

        unsigned int displ = 0;
        foreach (AttrDefs::iterator, i, attrs)
            newEnv.vars[i->first] = i->second.displ = displ++;

        foreach (AttrDefs::iterator, i, attrs)
            i->second.e->bindVars(i->second.inherited ? env : newEnv);
    }

    else
        foreach (AttrDefs::iterator, i, attrs)
            i->second.e->bindVars(env);

    foreach (DynamicAttrDefs::iterator, i, dynamicAttrs) {
        i->nameExpr->bindVars(*dynamicEnv);
        i->valueExpr->bindVars(*dynamicEnv);
    }
}

void ExprList::bindVars(const StaticEnv & env)
{
    foreach (vector<Expr *>::iterator, i, elems)
        (*i)->bindVars(env);
}

void ExprLambda::bindVars(const StaticEnv & env)
{
    StaticEnv newEnv(false, &env);

    unsigned int displ = 0;

    if (!arg.empty()) newEnv.vars[arg] = displ++;

    if (matchAttrs) {
        foreach (Formals::Formals_::iterator, i, formals->formals)
            newEnv.vars[i->name] = displ++;

        foreach (Formals::Formals_::iterator, i, formals->formals)
            if (i->def) i->def->bindVars(newEnv);
    }

    body->bindVars(newEnv);
}

void ExprLet::bindVars(const StaticEnv & env)
{
    StaticEnv newEnv(false, &env);

    unsigned int displ = 0;
    foreach (ExprAttrs::AttrDefs::iterator, i, attrs->attrs)
        newEnv.vars[i->first] = i->second.displ = displ++;

    foreach (ExprAttrs::AttrDefs::iterator, i, attrs->attrs)
        i->second.e->bindVars(i->second.inherited ? env : newEnv);

    body->bindVars(newEnv);
}

void ExprWith::bindVars(const StaticEnv & env)
{
    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    unsigned int level;
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

void ExprBuiltin::bindVars(const StaticEnv & env)
{
}

void ExprConcatStrings::bindVars(const StaticEnv & env)
{
    foreach (vector<Expr *>::iterator, i, *es)
        (*i)->bindVars(env);
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
    return (format("%1% at %2%") % (name.set() ? "`" + (string) name + "'" : "anonymous function") % pos).str();
}



/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    foreach (Symbols::const_iterator, i, symbols)
        n += i->size();
    return n;
}


}
