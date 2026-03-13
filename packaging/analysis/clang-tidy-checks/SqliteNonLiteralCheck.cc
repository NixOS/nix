#include "SqliteNonLiteralCheck.h"

#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void SqliteNonLiteralCheck::registerMatchers(MatchFinder * Finder)
{
    // Match calls to sqlite3_exec where the 2nd argument (SQL string)
    // is not a string literal.
    Finder->addMatcher(
        callExpr(callee(functionDecl(hasName("sqlite3_exec"))), hasArgument(1, expr().bind("sql-arg")))
            .bind("exec-call"),
        this);
}

bool SqliteNonLiteralCheck::isSafeExpression(const clang::Expr * E) const
{
    E = E->IgnoreParenImpCasts();

    // String literals are always safe.
    if (llvm::isa<clang::StringLiteral>(E))
        return true;

    // A DeclRefExpr to a constexpr or static const variable is safe.
    if (const auto * DRE = llvm::dyn_cast<clang::DeclRefExpr>(E)) {
        if (const auto * VD = llvm::dyn_cast<clang::VarDecl>(DRE->getDecl())) {
            if (VD->isConstexpr())
                return true;
            if (VD->getType().isConstQualified() && VD->getStorageClass() == clang::SC_Static)
                return true;
            // const char[] / const char* at namespace scope
            if (VD->getType().isConstQualified() && !VD->isLocalVarDecl())
                return true;
        }
    }

    return false;
}

void SqliteNonLiteralCheck::check(const MatchFinder::MatchResult & Result)
{
    const auto * Call = Result.Nodes.getNodeAs<clang::CallExpr>("exec-call");
    const auto * SqlArg = Result.Nodes.getNodeAs<clang::Expr>("sql-arg");

    if (!Call || !SqlArg)
        return;

    if (isSafeExpression(SqlArg))
        return;

    diag(
        SqlArg->getBeginLoc(),
        "sqlite3_exec() called with non-literal SQL string; "
        "prefer prepared statements or string literals to prevent SQL injection")
        << SqlArg->getSourceRange();
}

} // namespace nix::tidy
