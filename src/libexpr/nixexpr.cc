#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/util/util.hh"
#include "nix/expr/print.hh"

#include <cstdlib>
#include <sstream>

#include "nix/util/strings-inline.hh"

namespace nix {

Counter Expr::nrExprs;

ExprBlackHole eBlackHole;

// FIXME: remove, because *symbols* are abstract and do not have a single
//        textual representation; see printIdentifier()
std::ostream & operator<<(std::ostream & str, const SymbolStr & symbol)
{
    std::string_view s = symbol;
    return printIdentifier(str, s);
}

void Expr::show(const SymbolTable & symbols, std::ostream & str) const
{
    unreachable();
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
    printLiteralString(str, v.string_view());
}

void ExprPath::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << v.pathStrView();
}

void ExprVar::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << symbols[name];
}

void ExprSelect::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "(";
    e->show(symbols, str);
    str << ")." << showAttrSelectionPath(symbols, getAttrPath());
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
    str << ") ? " << showAttrSelectionPath(symbols, attrPath) << ")";
}

void ExprAttrs::showBindings(const SymbolTable & symbols, std::ostream & str) const
{
    typedef const AttrDefs::value_type * Attr;
    std::vector<Attr> sorted;
    for (auto & i : *attrs)
        sorted.push_back(&i);
    std::sort(sorted.begin(), sorted.end(), [&](Attr a, Attr b) {
        std::string_view sa = symbols[a->first], sb = symbols[b->first];
        return sa < sb;
    });
    std::vector<Symbol> inherits;
    // We can use the displacement as a proxy for the order in which the symbols were parsed.
    // The assignment of displacements should be deterministic, so that showBindings is deterministic.
    std::map<Displacement, std::vector<Symbol>> inheritsFrom;
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
            inheritsFrom[from.displ].push_back(i->first);
            break;
        }
        }
    }
    if (!inherits.empty()) {
        str << "inherit";
        for (auto sym : inherits)
            str << " " << symbols[sym];
        str << "; ";
    }
    for (const auto & [from, syms] : inheritsFrom) {
        str << "inherit (";
        (*inheritFromExprs)[from]->show(symbols, str);
        str << ")";
        for (auto sym : syms)
            str << " " << symbols[sym];
        str << "; ";
    }
    for (auto & i : sorted) {
        if (i->second.kind == AttrDef::Kind::Plain) {
            str << symbols[i->first] << " = ";
            i->second.e->show(symbols, str);
            str << "; ";
        }
    }
    for (auto & i : *dynamicAttrs) {
        str << "\"${";
        i.nameExpr->show(symbols, str);
        str << "}\" = ";
        i.valueExpr->show(symbols, str);
        str << "; ";
    }
}

void ExprAttrs::show(const SymbolTable & symbols, std::ostream & str) const
{
    if (recursive)
        str << "rec ";
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
    if (auto formals = getFormals()) {
        str << "{ ";
        bool first = true;
        // the natural Symbol ordering is by creation time, which can lead to the
        // same expression being printed in two different ways depending on its
        // context. always use lexicographic ordering to avoid this.
        for (auto & i : formals->lexicographicOrder(symbols)) {
            if (first)
                first = false;
            else
                str << ", ";
            str << symbols[i.name];
            if (i.def) {
                str << " ? ";
                i.def->show(symbols, str);
            }
        }
        if (ellipsis) {
            if (!first)
                str << ", ";
            str << "...";
        }
        str << " }";
        if (arg)
            str << " @ ";
    }
    if (arg)
        str << symbols[arg];
    str << ": ";
    body->show(symbols, str);
    str << ")";
}

void ExprCall::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << '(';
    fun->show(symbols, str);
    for (auto e : *args) {
        str << ' ';
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
    for (auto & i : es) {
        if (first)
            first = false;
        else
            str << " + ";
        i.second->show(symbols, str);
    }
    str << ")";
}

void ExprPos::show(const SymbolTable & symbols, std::ostream & str) const
{
    str << "__curPos";
}

std::string showAttrSelectionPath(const SymbolTable & symbols, std::span<const AttrName> attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first)
            out << '.';
        else
            first = false;
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
    unreachable();
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
    for (curEnv = env.get(), level = 0; curEnv; curEnv = curEnv->up.get(), level++) {
        if (curEnv->isWith) {
            if (withLevel == -1)
                withLevel = level;
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
        es.error<UndefinedVarError>("undefined variable '%1%'", es.symbols[name]).atPos(pos).debugThrow();
    for (auto * e = env.get(); e && !fromWith; e = e->up.get())
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
    if (def)
        def->bindVars(es, env);
    for (auto & i : getAttrPath())
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

std::shared_ptr<const StaticEnv>
ExprAttrs::bindInheritSources(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
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
    auto inner = std::make_shared<StaticEnv>(nullptr, env, 0);
    for (auto from : *inheritFromExprs)
        from->bindVars(es, env);

    return inner;
}

void ExprAttrs::moveDataToAllocator(std::pmr::polymorphic_allocator<char> & alloc)
{
    AttrDefs newAttrs{std::move(*attrs), alloc};
    attrs.emplace(std::move(newAttrs), alloc);
    DynamicAttrDefs newDynamicAttrs{std::move(*dynamicAttrs), alloc};
    dynamicAttrs.emplace(std::move(newDynamicAttrs), alloc);
    if (inheritFromExprs)
        inheritFromExprs = std::make_unique<std::pmr::vector<Expr *>>(std::move(*inheritFromExprs), alloc);
}

void ExprAttrs::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    moveDataToAllocator(es.mem.exprs.alloc);

    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    if (recursive) {
        auto newEnv = [&]() -> std::shared_ptr<const StaticEnv> {
            auto newEnv = std::make_shared<StaticEnv>(nullptr, env, attrs->size());

            Displacement displ = 0;
            for (auto & i : *attrs)
                newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
            return newEnv;
        }();

        // No need to sort newEnv since attrs is in sorted order.

        auto inheritFromEnv = bindInheritSources(es, newEnv);
        for (auto & i : *attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(newEnv, env, inheritFromEnv));

        for (auto & i : *dynamicAttrs) {
            i.nameExpr->bindVars(es, newEnv);
            i.valueExpr->bindVars(es, newEnv);
        }
    } else {
        auto inheritFromEnv = bindInheritSources(es, env);

        for (auto & i : *attrs)
            i.second.e->bindVars(es, i.second.chooseByKind(env, env, inheritFromEnv));

        for (auto & i : *dynamicAttrs) {
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

    auto newEnv =
        std::make_shared<StaticEnv>(nullptr, env, (getFormals() ? getFormals()->formals.size() : 0) + (!arg ? 0 : 1));

    Displacement displ = 0;

    if (arg)
        newEnv->vars.emplace_back(arg, displ++);

    if (auto formals = getFormals()) {
        for (auto & i : formals->formals)
            newEnv->vars.emplace_back(i.name, displ++);

        newEnv->sort();

        for (auto & i : formals->formals)
            if (i.def)
                i.def->bindVars(es, newEnv);
    }

    body->bindVars(es, newEnv);
}

void ExprCall::moveDataToAllocator(std::pmr::polymorphic_allocator<char> & alloc)
{
    std::pmr::vector<Expr *> newArgs{std::move(*args), alloc};
    args.emplace(std::move(newArgs), alloc);
}

void ExprCall::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    moveDataToAllocator(es.mem.exprs.alloc);
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));

    fun->bindVars(es, env);
    for (auto e : *args)
        e->bindVars(es, env);
}

void ExprLet::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    attrs->moveDataToAllocator(es.mem.exprs.alloc);
    auto newEnv = [&]() -> std::shared_ptr<const StaticEnv> {
        auto newEnv = std::make_shared<StaticEnv>(nullptr, env, attrs->attrs->size());

        Displacement displ = 0;
        for (auto & i : *attrs->attrs)
            newEnv->vars.emplace_back(i.first, i.second.displ = displ++);
        return newEnv;
    }();

    // No need to sort newEnv since attrs->attrs is in sorted order.

    auto inheritFromEnv = attrs->bindInheritSources(es, newEnv);
    for (auto & i : *attrs->attrs)
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
    for (auto * e = env.get(); e && !parentWith; e = e->up.get())
        parentWith = e->isWith;

    /* Does this `with' have an enclosing `with'?  If so, record its
       level so that `lookupVar' can look up variables in the previous
       `with' if this one doesn't contain the desired attribute. */
    const StaticEnv * curEnv;
    Level level;
    prevWith = 0;
    for (curEnv = env.get(), level = 1; curEnv; curEnv = curEnv->up.get(), level++)
        if (curEnv->isWith) {
            assert(level <= std::numeric_limits<uint32_t>::max());
            prevWith = level;
            break;
        }

    attrs->bindVars(es, env);
    auto newEnv = std::make_shared<StaticEnv>(this, env);
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

    for (auto & i : this->es)
        i.second->bindVars(es, env);
}

void ExprPos::bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env)
{
    if (es.debugRepl)
        es.exprEnvs.insert(std::make_pair(this, env));
}

/* Finding positions in expressions for error traces. */

std::partial_ordering ExprOpHasAttr::comparePos(PosIdx otherPos) const noexcept
{
    return e->comparePos(otherPos);
}

std::partial_ordering ExprOpNot::comparePos(PosIdx otherPos) const noexcept
{
    return e->comparePos(otherPos);
}

/**
 * Utility for aggregating invididual comparisons between points in an interval
 * and a point into an overall comparison between the entire interval and the
 * point.
 *
 * comparePos methods, if implemented naively, would walk the entire expression
 * tree. To try to make them a little less expensive, we exploit the (mostly)
 * ordered nature of the tree and perform the comparison in two directions:
 * first from the left, until we find a value that is less than the position in
 * question; at which point we pivot to searching from the right. If we also
 * find a value that is greater than the position in question, we can terminate
 * the search. (Of course, if when searching from the left, we find a value
 * that is *greater* than the position in question, we can also terminate
 * early; same for the other side.)
 */
struct PartialOrderingAccumulator
{
    /**
     * The result of the comparison. Is kept updated throughout, and is used to
     * record whether a value less than or greater than the needle has been
     * found yet.
     */
    std::partial_ordering result = std::partial_ordering::unordered;

    /**
     * Accumulate `po` into `result`. Return true if the result is known after
     * the update.
     */
    bool updateAgnostic(std::partial_ordering po)
    {
        if (po == std::partial_ordering::unordered) {
            return false;
        }
        if (po < 0) {
            if (!(result > 0)) {
                result = std::partial_ordering::less;
                return false;
            }
        } else if (po > 0) {
            if (!(result < 0)) {
                result = std::partial_ordering::greater;
                return false;
            }
        }
        result = std::partial_ordering::equivalent;
        return true;
    }

    /**
     * As `updateAgnostic`, but optimized for when every comparison to the left
     * of this one has already been checked.
     */
    bool updateLeft(std::partial_ordering po)
    {
        if (po == std::partial_ordering::unordered) {
            return false;
        }
        if (po < 0 && !(result > 0)) {
            result = std::partial_ordering::less;
            return false;
        }
        result = po <= 0 || (po > 0 && result < 0) ? std::partial_ordering::equivalent : std::partial_ordering::greater;
        return true;
    }

    /**
     * As `updateAgnostic`, but optimized for when every comparison to the
     * right of this one has already been checked. Prefer searching from the
     * right with this method when `result < 0` (meaning that the interval is
     * already known to contain a value less than the needle).
     */
    bool updateRight(std::partial_ordering po)
    {
        if (po == std::partial_ordering::unordered) {
            return false;
        }
        if (po > 0 && !(result < 0)) {
            result = std::partial_ordering::greater;
            return false;
        }
        result = po >= 0 || (po < 0 && result > 0) ? std::partial_ordering::equivalent : std::partial_ordering::less;
        return true;
    }
};

/**
 * Search a sequence of expressions, known to be in order of their positions
 * and found to the right of all other positions (which must have already been
 * checked) in the subexpression.
 */
static std::partial_ordering
updateAndEndWithExprs(PartialOrderingAccumulator & poa, const std::span<Expr * const> elems, PosIdx otherPos)
{
    Expr * const * lastChecked = nullptr;
    if (!(poa.result < 0)) {
        for (auto & elem : elems) {
            if (poa.updateLeft(elem->comparePos(otherPos)) || &elem == &elems.back())
                return poa.result;

            if (poa.result < 0) {
                lastChecked = &elem;
                break;
            }
        }
    }
    for (auto & elem : std::views::reverse(elems)) {
        if (&elem == lastChecked || poa.updateRight(elem->comparePos(otherPos)))
            break;
    }
    return poa.result;
}

std::partial_ordering ExprList::comparePos(PosIdx otherPos) const noexcept
{
    if (otherPos == noPos)
        return std::partial_ordering::unordered;

    PartialOrderingAccumulator poa{};
    return updateAndEndWithExprs(poa, elems, otherPos);
}

std::partial_ordering ExprCall::comparePos(PosIdx otherPos) const noexcept
{
    if (otherPos == noPos)
        return std::partial_ordering::unordered;

    PartialOrderingAccumulator poa{};

    // Only check pos if we know this is not a desugared infix operator. In
    // those cases, pos points to a location between the first and second
    // arguments. So comparing with the argument positions only is sufficient,
    // and comparing with pos would lead to an incorrect result if otherPos is
    // contained in the first operand.
    PosIdx p;
    if (args->empty() || (p = args->front()->getPos()) == noPos || p >= pos) {
        if (poa.updateLeft(pos.partialCompare(otherPos)))
            return poa.result;
    }

    return updateAndEndWithExprs(poa, std::span(*args), otherPos);
}

std::partial_ordering ExprConcatStrings::comparePos(PosIdx otherPos) const noexcept
{
    if (otherPos == noPos)
        return std::partial_ordering::unordered;

    PartialOrderingAccumulator poa{};

    // We ignore pos, since as in the ExprCall case, it might be an infix
    // operator. Unlike in the ExprCall case, we never need to check the
    // position of the outer string, since it can't be the position of a
    // subexpression. Checking only the operands/interpolated values is
    // sufficient.
    std::pair<PosIdx, Expr *> * lastChecked = nullptr;
    for (auto & e : es) {
        if (poa.updateLeft(e.first.partialCompare(otherPos)))
            return poa.result;

        if (poa.result < 0) {
            lastChecked = &e;
            break;
        }
    }

    for (auto & e : std::views::reverse(es)) {
        if (&e == lastChecked || poa.updateRight(e.second->comparePos(otherPos))
            || poa.updateRight(e.first.partialCompare(otherPos)))
            return poa.result;
    }

    return poa.result;
}

std::partial_ordering ExprAttrs::comparePos(PosIdx otherPos) const noexcept
{
    if (otherPos == noPos)
        return std::partial_ordering::unordered;

    PartialOrderingAccumulator poa{};

    if (poa.updateLeft(pos.partialCompare(otherPos)))
        return poa.result;

    // Attributes aren't position-ordered, so this can't use the smart
    // reversing search we use for lists.
    for (auto & [_, attr] : *attrs) {
        if (poa.updateAgnostic(attr.pos.partialCompare(otherPos)) || poa.updateAgnostic(attr.e->comparePos(otherPos)))
            return poa.result;
    }
    for (auto & attr : *dynamicAttrs) {
        if (poa.updateAgnostic(attr.pos.partialCompare(otherPos))
            || poa.updateAgnostic(attr.nameExpr->comparePos(otherPos))
            || poa.updateAgnostic(attr.valueExpr->comparePos(otherPos)))
            return poa.result;
    }

    return poa.result;
}

// This macro assumes a free PosIdx otherPos.
#define COMPARE_POS_BODY_TEMPLATE(updates)       \
    if (otherPos == noPos)                       \
        return std::partial_ordering::unordered; \
    PartialOrderingAccumulator poa{};            \
    updates;                                     \
    return poa.result;

#define COMPARE_POS_TEMPLATE(Cls, updates)                                \
    std::partial_ordering Cls::comparePos(PosIdx otherPos) const noexcept \
    {                                                                     \
        COMPARE_POS_BODY_TEMPLATE(updates)                                \
    }

// These macros assume a free PartialOrderingAccumulator poa.
#define UPDATE_L2(e1, e2) (poa.updateLeft(e1) || poa.updateLeft(e2))
#define UPDATE_L3(e1, e2, e3) (poa.updateLeft(e1) || (poa.result < 0 ? UPDATE_R2(e2, e3) : UPDATE_L2(e2, e3)))
#define UPDATE_L4(e1, e2, e3, e4) \
    (poa.updateLeft(e1) || (poa.result < 0 ? UPDATE_R3(e2, e3, e4) : UPDATE_L3(e2, e3, e4)))
#define UPDATE_R2(e1, e2) (poa.updateRight(e2) || poa.updateRight(e1))
#define UPDATE_R3(e1, e2, e3) (poa.updateRight(e3) || UPDATE_R2(e1, e2))

COMPARE_POS_TEMPLATE(ExprLambda, UPDATE_L2(pos.partialCompare(otherPos), body->comparePos(otherPos)));
COMPARE_POS_TEMPLATE(ExprLet, UPDATE_L2(attrs->comparePos(otherPos), body->comparePos(otherPos)));

COMPARE_POS_TEMPLATE(
    ExprAssert, UPDATE_L3(pos.partialCompare(otherPos), cond->comparePos(otherPos), body->comparePos(otherPos)));

COMPARE_POS_TEMPLATE(
    ExprSelect,
    UPDATE_L3(
        pos.partialCompare(otherPos),
        e->comparePos(otherPos),
        def ? def->comparePos(otherPos) : std::partial_ordering::unordered));

COMPARE_POS_TEMPLATE(
    ExprWith, UPDATE_L3(pos.partialCompare(otherPos), attrs->comparePos(otherPos), body->comparePos(otherPos)));

std::partial_ordering Expr::binOpComparePos(const Expr * e1, PosIdx pos, const Expr * e2, PosIdx otherPos)
{
    COMPARE_POS_BODY_TEMPLATE(
        UPDATE_L3(e1->comparePos(otherPos), pos.partialCompare(otherPos), e2->comparePos(otherPos)));
}

COMPARE_POS_TEMPLATE(
    ExprIf,
    UPDATE_L4(
        pos.partialCompare(otherPos),
        cond->comparePos(otherPos),
        then->comparePos(otherPos),
        else_->comparePos(otherPos)));

#undef COMPARE_POS_BODY_TEMPLATE
#undef COMPARE_POS_TEMPLATE
#undef UPDATE_L2
#undef UPDATE_L3
#undef UPDATE_L4
#undef UPDATE_R2
#undef UPDATE_R3

/* Storing function names. */

void Expr::setName(Symbol name) {}

void ExprLambda::setName(Symbol name)
{
    this->name = name;
    body->setName(name);
}

std::string ExprLambda::showNamePos(const EvalState & state) const
{
    std::string id(name ? concatStrings("'", state.symbols[name], "'") : "anonymous function");
    return fmt("%1% at %2%", id, state.positions[pos]);
}

void ExprLambda::setDocComment(DocComment docComment)
{
    // RFC 145 specifies that the innermost doc comment wins.
    // See https://github.com/NixOS/rfcs/blob/master/rfcs/0145-doc-strings.md#ambiguous-placement
    if (!this->docComment) {
        this->docComment = docComment;

        // Curried functions are defined by putting a function directly
        // in the body of another function. To render docs for those, we
        // need to propagate the doc comment to the innermost function.
        //
        // If we have our own comment, we've already propagated it, so this
        // belongs in the same conditional.
        body->setDocComment(docComment);
    }
};

/* Symbol table. */

size_t SymbolTable::totalSize() const
{
    size_t n = 0;
    dump([&](SymbolStr s) { n += s.size(); });
    return n;
}

std::string DocComment::getInnerText(const PosTable & positions) const
{
    auto beginPos = positions[begin];
    auto endPos = positions[end];
    auto docCommentStr = beginPos.getSnippetUpTo(endPos).value_or("");

    // Strip "/**" and "*/"
    constexpr size_t prefixLen = 3;
    constexpr size_t suffixLen = 2;
    std::string docStr = docCommentStr.substr(prefixLen, docCommentStr.size() - prefixLen - suffixLen);
    if (docStr.empty())
        return {};
    // Turn the now missing "/**" into indentation
    docStr = "   " + docStr;
    // Strip indentation (for the whole, potentially multi-line string)
    docStr = stripIndentation(docStr);
    return docStr;
}

/* ‘Cursed or’ handling.
 *
 * In parser.y, every use of expr_select in a production must call one of the
 * two below functions.
 *
 * To be removed by https://github.com/NixOS/nix/pull/11121
 */

void ExprCall::resetCursedOr()
{
    cursedOrEndPos.reset();
}

void ExprCall::warnIfCursedOr(const SymbolTable & symbols, const PosTable & positions)
{
    if (cursedOrEndPos.has_value()) {
        std::ostringstream out;
        out << "at " << positions[pos]
            << ": "
               "This expression uses `or` as an identifier in a way that will change in a future Nix release.\n"
               "Wrap this entire expression in parentheses to preserve its current meaning:\n"
               "    ("
            << positions[pos].getSnippetUpTo(positions[*cursedOrEndPos]).value_or("could not read expression")
            << ")\n"
               "Give feedback at https://github.com/NixOS/nix/pull/11121";
        warn(out.str());
    }
}

} // namespace nix
