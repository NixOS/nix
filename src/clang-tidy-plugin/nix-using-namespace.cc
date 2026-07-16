#include "nix-using-namespace.hh"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>

namespace nix::clang_tidy {

void UsingNamespaceInNamespaceScopeCheck::registerMatchers(clang::ast_matchers::MatchFinder * Finder)
{
    Finder->addMatcher(clang::ast_matchers::usingDirectiveDecl().bind("usingNamespace"), this);
}

void UsingNamespaceInNamespaceScopeCheck::check(const clang::ast_matchers::MatchFinder::MatchResult & Result)
{
    const auto * U = Result.Nodes.getNodeAs<clang::UsingDirectiveDecl>("usingNamespace");
    const clang::SourceLocation Loc = U->getBeginLoc();
    if (U->isImplicit() || !Loc.isValid() || U->getParentFunctionOrMethod())
        return;

    diag(
        Loc,
        "do not use using namespace directive in namespace scopes - keep those local to functions or explicitly qualify names."
        "This is to reduce namespace pollution with unity builds.");
}

} // namespace nix::clang_tidy
