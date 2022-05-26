#include "nixexpr.hh"
#include "derivations.hh"
#include "eval.hh"
#include "symbol-table.hh"
#include "util.hh"

#include <cstdlib>

namespace nix {

/* Displaying abstract syntax trees. */

static void showString(std::ostream & str, std::string_view s)
{
    str << '"';
    for (auto c : s)
        if (c == '"' || c == '\\' || c == '$') str << "\\" << c;
        else if (c == '\n') str << "\\n";
        else if (c == '\r') str << "\\r";
        else if (c == '\t') str << "\\t";
        else str << c;
    str << '"';
}

std::ostream & operator <<(std::ostream & str, const SymbolStr & symbol)
{
    std::string_view s = symbol;

    if (s.empty())
        str << "\"\"";
    else if (s == "if") // FIXME: handle other keywords
        str << '"' << s << '"';
    else {
        char c = s[0];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
            showString(str, s);
            return str;
        }
        for (auto c : s)
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '\'' || c == '-')) {
                showString(str, s);
                return str;
            }
        str << s;
    }
    return str;
}

void Expr::show(const SymbolTable & symbols, std::ostream & str) const
{
    abort();
}

void ExprInt::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << n;
}

void ExprFloat::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << nf;
}

void ExprString::show(const SymbolTable & symbols, std::ostream & str) const
{
    showString(str, s);
}

void ExprPath::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << path;
}

void ExprVar::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << symbols[name];
}

void ExprSelect::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(";
    e->show(symbols, str);
    str << ")." << showAttrPath(symbols, attrPath);
    if (def) {
        str << " or (";
        def->show(symbols, str);
        str << ")";
    }
}

void ExprOpHasAttr::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "((";
    e->show(symbols, str);
    str << ") ? " << showAttrPath(symbols, attrPath) << ")";
}

void ExprAttrs::show(const SymbolTable & symbols, std::ostream & str) const
{
    if (recursive) str << "rec ";
    str << "{ ";
    typedef const decltype(attrs)::value_type * Attr;
    std::vector<Attr> sorted;
    for (auto & i : attrs) sorted.push_back(&i);
    std::sort(sorted.begin(), sorted.end(), [&](Attr a, Attr b) {
        std::string_view sa = symbols[a->first], sb = symbols[b->first];
        return sa < sb;
    });
    for (auto & i : sorted) {
        if (i->second.inherited)
            str << "inherit " << symbols[i->first] << " " << "; ";
        else {
            str << symbols[i->first] << " = ";
            i->second.e->show(symbols, str);
            str << "; ";
        }
    }
    for (auto & i : dynamicAttrs) {
        str << "\"${";
        i.nameExpr->show(symbols, str);
        str << "}\" = ";
        i.valueExpr->show(symbols, str);
        str << "; ";
    }
    str << "}";
}

void ExprList::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "[ ";
    for (auto & i : elems) {
        str << "(";
        i->show(symbols, str);
        str << ") ";
    }
    str << "]";
}

void ExprLambda::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(";
    if (hasFormals()) {
        str << "{ ";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ", ";
            str << symbols[i.name];
            if (i.def) {
                str << " ? ";
                i.def->show(symbols, str);
            }
        }
        if (formals->ellipsis) {
            if (!first) str << ", ";
            str << "...";
        }
        str << " }";
        if (arg) str << " @ ";
    }
    if (arg) str << symbols[arg];
    str << ": ";
    body->show(symbols, str);
    str << ")";
}

void ExprCall::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << '(';
    fun->show(symbols, str);
    for (auto e : args) {
        str <<  ' ';
        e->show(symbols, str);
    }
    str << ')';
}

void ExprLet::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(let ";
    for (auto & i : attrs->attrs)
        if (i.second.inherited) {
            str << "inherit " << symbols[i.first] << "; ";
        }
        else {
            str << symbols[i.first] << " = ";
            i.second.e->show(symbols, str);
            str << "; ";
        }
    str << "in ";
    body->show(symbols, str);
    str << ")";
}

void ExprWith::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(with ";
    attrs->show(symbols, str);
    str << "; ";
    body->show(symbols, str);
    str << ")";
}

void ExprIf::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(if ";
    cond->show(symbols, str);
    str << " then ";
    then->show(symbols, str);
    str << " else ";
    else_->show(symbols, str);
    str << ")";
}

void ExprAssert::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "assert ";
    cond->show(symbols, str);
    str << "; ";
    body->show(symbols, str);
}

void ExprOpNot::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(! ";
    e->show(symbols, str);
    str << ")";
}

void ExprConcatStrings::show(const SymbolTable & symbols, std::ostream & str) const
{
    bool first = true;
    str << "(";
    for (auto & i : *es) {
        if (first) first = false; else str << " + ";
        i.second->show(symbols, str);
    }
    str << ")";
}

void ExprPos::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "__curPos";
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
                f % (const std::string &) pos.file;
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


std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << '.'; else first = false;
        if (i.symbol)
            out << symbols[i.symbol];
        else {
            out << "\"${";
            i.expr->show(symbols, out);
            out << "}\"";
        }
    }
    return out.str();
}



/* Computing levels/displacements for variables. */

void Expr::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    abort();
}

void ExprInt::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}

void ExprFloat::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}

void ExprString::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}

void ExprPath::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}

void ExprVar::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    /* Check whether the variable appears in the environment.  If so,
       set its level and displacement. */
    const StaticEnv * curEnv;
    Level level;
    int withLevel = -1;
    for (curEnv = env.get(), level = 0; curEnv; curEnv = curEnv->up, level++) {
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
            .msg = hintfmt("undefined variable '%1%'", es.symbols[name]),
            .errPos = es.positions[pos]
        });
    fromWith = true;
    this->level = withLevel;
}

void ExprSelect::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
    if (def) def->bindVars(es, env);
    for (auto & i : attrPath)
        if (!i.symbol)
            i.expr->bindVars(es, env);
}

void ExprOpHasAttr::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
    for (auto & i : attrPath)
        if (!i.symbol)
            i.expr->bindVars(es, env);
}

void ExprAttrs::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    if (recursive) {
        auto newEnv = std::make_shared<StaticEnv>(false, env.get(), recursive ? attrs.size() : 0);

        Displacement displ = 0;
        for (auto & i : attrs)
            newEnv->vars.emplace_back(i.first, i.second.displ = displ++);

        // No need to sort newEnv since attrs is in sorted order.

        for (auto & i : attrs)
            i.second.e->bindVars(es, i.second.inherited ? env : newEnv);

        for (auto & i : dynamicAttrs) {
            i.nameExpr->bindVars(es, newEnv);
            i.valueExpr->bindVars(es, newEnv);
        }
    }
    else {
        for (auto & i : attrs)
            i.second.e->bindVars(es, env);

        for (auto & i : dynamicAttrs) {
            i.nameExpr->bindVars(es, env);
            i.valueExpr->bindVars(es, env);
        }
    }
}

void ExprList::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    for (auto & i : elems)
        i->bindVars(es, env);
}

void ExprLambda::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    auto newEnv = std::make_shared<StaticEnv>(
        false, env.get(),
        (hasFormals() ? formals->formals.size() : 0) +
        (!arg ? 0 : 1));

    Displacement displ = 0;

    if (arg) newEnv->vars.emplace_back(arg, displ++);

    if (hasFormals()) {
        for (auto & i : formals->formals)
            newEnv->vars.emplace_back(i.name, displ++);

        newEnv->sort();

        for (auto & i : formals->formals)
            if (i.def) i.def->bindVars(es, newEnv);
    }

    body->bindVars(es, newEnv);
}

void ExprCall::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    fun->bindVars(es, env);
    for (auto e : args)
        e->bindVars(es, env);
}

void ExprLet::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    auto newEnv = std::make_shared<StaticEnv>(false, env.get(), attrs->attrs.size());

    Displacement displ = 0;
    for (auto & i : attrs->attrs)
        newEnv->vars.emplace_back(i.first, i.second.displ = displ++);

    // No need to sort newEnv since attrs->attrs is in sorted order.

    for (auto & i : attrs->attrs)
        i.second.e->bindVars(es, i.second.inherited ? env : newEnv);

    body->bindVars(es, newEnv);
}

void ExprWith::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    Level level;
    prevWith = 0;
    for (curEnv = env.get(), level = 1; curEnv; curEnv = curEnv->up, level++)
        if (curEnv->isWith) {
            prevWith = level;
            break;
        }

    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    attrs->bindVars(es, env);
    auto newEnv = std::make_shared<StaticEnv>(true, env.get());
    body->bindVars(es, newEnv);
}

void ExprIf::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    cond->bindVars(es, env);
    then->bindVars(es, env);
    else_->bindVars(es, env);
}

void ExprAssert::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    cond->bindVars(es, env);
    body->bindVars(es, env);
}

void ExprOpNot::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    e->bindVars(es, env);
}

void ExprConcatStrings::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    for (auto & i : *this->es)
        i.second->bindVars(es, env);
}

void ExprPos::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}


/* Storing function names. */

void Expr::setName(Symbol name)
{
}


void ExprLambda::setName(Symbol name)
{
    this->name = name;
    body->setName(name);
}


std::string ExprLambda::showNamePos(const EvalState & state) const
{
    std::string id(name
        ? concatStrings("'", state.symbols[name], "'")
        : "anonymous function");
    return fmt("%1% at %2%", id, state.positions[pos]);
}



/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    dump([&] (const std::string & s) { n += s.size(); });
    return n;
}

}
