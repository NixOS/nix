#include "nixexpr.hh"
#include "derivations.hh"
#include "eval.hh"
#include "symbol-table.hh"
#include "util.hh"
#include "print.hh"

#include <cstdlib>

namespace nix {

unsigned long Expr::nrExprs = 0;

ExprBlackHole eBlackHole;

// FIXME: remove, because *symbols* are abstract and do not have a single
//        textual representation; see printIdentifier()
std::ostream & operator <<(std::ostream & str, const SymbolStr & symbol)
{
    std::string_view s = symbol;
    return printIdentifier(str, s);
}

void Expr::show(const SymbolTable & symbols, std::ostream & str) const
{
    abort();
}

void ExprInt::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << v.integer();
}

void ExprFloat::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << v.fpoint();
}

void ExprString::show(const SymbolTable & symbols, std::ostream & str) const
{
    printLiteralString(str, s);
}

void ExprPath::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << s;
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

void ExprAttrs::showBindings(const SymbolTable & symbols, std::ostream & str) const
{
    typedef const decltype(attrs)::value_type * Attr;
    std::vector<Attr> sorted;
    for (auto & i : attrs) sorted.push_back(&i);
    std::sort(sorted.begin(), sorted.end(), [&](Attr a, Attr b) {
        std::string_view sa = symbols[a->first], sb = symbols[b->first];
        return sa < sb;
    });
    std::vector<Symbol> inherits;
    std::map<ExprInheritFrom *, std::vector<Symbol>> inheritsFrom;
    for (auto & i : sorted) {
        switch (i->second.kind) {
        case AttrDef::Kind::Plain:
            break;
        case AttrDef::Kind::Inherited:
            inherits.push_back(i->first);
            break;
        case AttrDef::Kind::InheritedFrom: {
            auto & select = dynamic_cast<ExprSelect &>(*i->second.e);
            auto & from = dynamic_cast<ExprInheritFrom &>(*select.e);
            inheritsFrom[&from].push_back(i->first);
            break;
        }
        }
    }
    if (!inherits.empty()) {
        str << "inherit";
        for (auto sym : inherits) str << " " << symbols[sym];
        str << "; ";
    }
    for (const auto & [from, syms] : inheritsFrom) {
        str << "inherit (";
        (*inheritFromExprs)[from->displ]->show(symbols, str);
        str << ")";
        for (auto sym : syms) str << " " << symbols[sym];
        str << "; ";
    }
    for (auto & i : sorted) {
        if (i->second.kind == AttrDef::Kind::Plain) {
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
}

void ExprAttrs::show(const SymbolTable & symbols, std::ostream & str) const
{
    if (recursive) str << "rec ";
    str << "{ ";
    showBindings(symbols, str);
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
        // the natural Symbol ordering is by creation time, which can lead to the
        // same expression being printed in two different ways depending on its
        // context. always use lexicographic ordering to avoid this.
        for (auto & i : formals->lexicographicOrder(symbols)) {
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
    attrs->showBindings(symbols, str);
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

    fromWith = nullptr;

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
        es.error<UndefinedVarError>(
            "undefined variable '%1%'",
            es.symbols[name]
        ).atPos(pos).debugThrow();
    for (auto * e = env.get(); e && !fromWith; e = e->up)
        fromWith = e->isWith;
    this->level = withLevel;
}

void ExprInheritFrom::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
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

std::shared_ptr<const StaticEnv> ExprAttrs::bindInheritSources(
    EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (!inheritFromExprs)
        return nullptr;

    // the inherit (from) source values are inserted into an env of its own, which
    // does not introduce any variable names.
    // analysis must see an empty env, or an env that contains only entries with
    // otherwise unused names to not interfere with regular names. the parser
    // has already filled all exprs that access this env with appropriate level
    // and displacement, and nothing else is allowed to access it. ideally we'd
    // not even *have* an expr that grabs anything from this env since it's fully
    // invisible, but the evaluator does not allow for this yet.
    auto inner = std::make_shared<StaticEnv>(nullptr, env.get(), 0);
    for (auto from : *inheritFromExprs)
        from->bindVars(es, env);

    return inner;
}

void ExprAttrs::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    if (recursive) {
        auto newEnv = [&] () -> std::shared_ptr<const StaticEnv> {
            auto newEnv = std::make_shared<StaticEnv>(nullptr, env.get(), attrs.size());

            Displacement displ = 0;
            for (auto & i : attrs)
                newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
            return newEnv;
        }();

        // No need to sort newEnv since attrs is in sorted order.

        auto inheritFromEnv = bindInheritSources(es, newEnv);
        for (auto & i : attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(newEnv, env, inheritFromEnv));

        for (auto & i : dynamicAttrs) {
            i.nameExpr->bindVars(es, newEnv);
            i.valueExpr->bindVars(es, newEnv);
        }
    }
    else {
        auto inheritFromEnv = bindInheritSources(es, env);

        for (auto & i : attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(env, env, inheritFromEnv));

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
        nullptr, env.get(),
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
    auto newEnv = [&] () -> std::shared_ptr<const StaticEnv> {
        auto newEnv = std::make_shared<StaticEnv>(nullptr, env.get(), attrs->attrs.size());

        Displacement displ = 0;
        for (auto & i : attrs->attrs)
            newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
        return newEnv;
    }();

    // No need to sort newEnv since attrs->attrs is in sorted order.

    auto inheritFromEnv = attrs->bindInheritSources(es, newEnv);
    for (auto & i : attrs->attrs)
        i.second.e->bindVars(es, i.second.chooseByKind(newEnv, env, inheritFromEnv));

    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, newEnv));

    body->bindVars(es, newEnv);
}

void ExprWith::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    parentWith = nullptr;
    for (auto * e = env.get(); e && !parentWith; e = e->up)
        parentWith = e->isWith;

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

    attrs->bindVars(es, env);
    auto newEnv = std::make_shared<StaticEnv>(this, env.get());
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



/* Position table. */

Pos PosTable::operator[](PosIdx p) const
{
    auto origin = resolve(p);
    if (!origin)
        return {};

    const auto offset = origin->offsetOf(p);

    Pos result{0, 0, origin->origin};
    auto lines = this->lines.lock();
    auto linesForInput = (*lines)[origin->offset];

    if (linesForInput.empty()) {
        auto source = result.getSource().value_or("");
        const char * begin = source.data();
        for (Pos::LinesIterator it(source), end; it != end; it++)
            linesForInput.push_back(it->data() - begin);
        if (linesForInput.empty())
            linesForInput.push_back(0);
    }
    // as above: the first line starts at byte 0 and is always present
    auto lineStartOffset = std::prev(
        std::upper_bound(linesForInput.begin(), linesForInput.end(), offset));

    result.line = 1 + (lineStartOffset - linesForInput.begin());
    result.column = 1 + (offset - *lineStartOffset);
    return result;
}



/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    dump([&] (const std::string & s) { n += s.size(); });
    return n;
}

}
