#include "DanglingCStrCheck.h"

#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void DanglingCStrCheck::registerMatchers(MatchFinder *Finder) {
  // Match: varDecl whose initializer is a c_str()/data() call on a
  // std::basic_string object.  We do the temporary-vs-named check in
  // the check() callback to avoid matcher complexity with template
  // specialisation types.
  Finder->addMatcher(
      varDecl(
          hasType(pointerType(pointee(isAnyCharacter()))),
          hasInitializer(ignoringImplicit(
              cxxMemberCallExpr(
                  callee(cxxMethodDecl(
                      hasAnyName("c_str", "data"),
                      ofClass(hasName("::std::basic_string")))))
                  .bind("call"))))
          .bind("var"),
      this);
}

void DanglingCStrCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Var = Result.Nodes.getNodeAs<clang::VarDecl>("var");
  const auto *Call = Result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("call");

  if (!Var || !Call)
    return;

  const clang::Expr *Object = Call->getImplicitObjectArgument();
  if (!Object)
    return;

  Object = Object->IgnoreParenImpCasts();

  // If it's a reference to a named variable, it's safe.
  if (llvm::isa<clang::DeclRefExpr>(Object))
    return;

  // If it's a MemberExpr (e.g. this->field.c_str()), it's safe.
  if (llvm::isa<clang::MemberExpr>(Object))
    return;

  // If it's an ArraySubscriptExpr (e.g. vec[0].c_str()), it's safe
  // (the container outlives the statement).
  if (llvm::isa<clang::ArraySubscriptExpr>(Object))
    return;

  // Everything else is a temporary (function return, concatenation,
  // substr, etc.) — the pointer will dangle.
  diag(Call->getBeginLoc(),
       "result of '%0' on a temporary std::string stored in '%1'; "
       "the pointer will dangle after the temporary is destroyed")
      << Call->getMethodDecl()->getName() << Var->getName()
      << Call->getSourceRange();
}

} // namespace nix::tidy
