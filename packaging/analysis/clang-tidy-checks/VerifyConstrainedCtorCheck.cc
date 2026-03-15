#include "VerifyConstrainedCtorCheck.h"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void VerifyConstrainedCtorCheck::registerMatchers(MatchFinder *Finder) {
  // Match constructors that have at least one rvalue reference parameter.
  // These are the ones bugprone-forwarding-reference-overload flags.
  Finder->addMatcher(
      cxxConstructorDecl(
          hasAnyParameter(hasType(rValueReferenceType())),
          unless(isImplicit()),
          unless(isCopyConstructor()),
          unless(isMoveConstructor()))
          .bind("ctor"),
      this);
}

/// Check if a constructor has any associated constraints (trailing requires
/// clause on the function, or requires clause on the enclosing template).
static bool hasAnyConstraint(const clang::CXXConstructorDecl *Ctor) {
  // Check trailing requires clause: void f(U&&) requires C<U>
  if (Ctor->getTrailingRequiresClause())
    return true;

  // Check template-head requires clause: template<typename U> requires C<U>
  if (auto *FTD = Ctor->getDescribedFunctionTemplate()) {
    if (auto *TPL = FTD->getTemplateParameters()) {
      if (TPL->getRequiresClause())
        return true;
    }
  }

  // Also check if this is an instantiation of a constrained template
  if (auto *Primary = Ctor->getPrimaryTemplate()) {
    if (auto *TPL = Primary->getTemplateParameters()) {
      if (TPL->getRequiresClause())
        return true;
    }
    if (auto *Templated = llvm::dyn_cast<clang::CXXConstructorDecl>(
            Primary->getTemplatedDecl())) {
      if (Templated->getTrailingRequiresClause())
        return true;
    }
  }

  return false;
}

void VerifyConstrainedCtorCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Ctor = Result.Nodes.getNodeAs<clang::CXXConstructorDecl>("ctor");
  if (!Ctor)
    return;

  if (hasAnyConstraint(Ctor)) {
    emitVerified(Ctor->getLocation(), "forwardingRefOverload",
                 "constructor has a requires clause that prevents "
                 "forwarding-reference overload hijacking");
  } else {
    emitContradiction(Ctor->getLocation(), "forwardingRefOverload",
                      "constructor with rvalue-reference parameter has no "
                      "requires clause; forwarding-reference overload "
                      "hijacking is possible");
  }
}

} // namespace nix::tidy
