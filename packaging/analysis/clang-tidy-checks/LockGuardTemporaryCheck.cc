#include "LockGuardTemporaryCheck.h"

#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void LockGuardTemporaryCheck::registerMatchers(MatchFinder * Finder)
{
    // Match a CXXTemporaryObjectExpr (explicit temporary construction)
    // or CXXFunctionalCastExpr whose type is a lock guard class.
    // These are temporaries that get destroyed at the end of the
    // full-expression — i.e. the lock is released immediately.
    Finder->addMatcher(
        cxxTemporaryObjectExpr(
            hasType(cxxRecordDecl(hasAnyName("std::lock_guard", "std::scoped_lock", "std::unique_lock"))))
            .bind("temp-lock"),
        this);

    // Also match functional cast syntax: lock_guard<mutex>(m)
    Finder->addMatcher(
        cxxFunctionalCastExpr(
            hasType(cxxRecordDecl(hasAnyName("std::lock_guard", "std::scoped_lock", "std::unique_lock"))))
            .bind("cast-lock"),
        this);
}

void LockGuardTemporaryCheck::check(const MatchFinder::MatchResult & Result)
{
    const clang::Expr * E = nullptr;
    if (auto * Temp = Result.Nodes.getNodeAs<clang::CXXTemporaryObjectExpr>("temp-lock"))
        E = Temp;
    else if (auto * Cast = Result.Nodes.getNodeAs<clang::CXXFunctionalCastExpr>("cast-lock"))
        E = Cast;

    if (!E)
        return;

    diag(
        E->getBeginLoc(),
        "lock guard constructed as a temporary; it will be immediately "
        "destroyed and the mutex will not be held")
        << E->getSourceRange();
}

} // namespace nix::tidy
