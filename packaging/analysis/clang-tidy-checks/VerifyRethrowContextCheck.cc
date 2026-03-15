#include "VerifyRethrowContextCheck.h"

#include <clang/AST/ParentMapContext.h>
#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void VerifyRethrowContextCheck::registerMatchers(MatchFinder *Finder) {
  // Match bare throw; (CXXThrowExpr with no operand)
  Finder->addMatcher(
      cxxThrowExpr(unless(has(expr()))).bind("throw"),
      this);
}

void VerifyRethrowContextCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Throw = Result.Nodes.getNodeAs<clang::CXXThrowExpr>("throw");
  if (!Throw)
    return;

  auto &Ctx = *Result.Context;
  bool FoundCatch = false;
  bool FoundTry = false;

  // Walk ancestors to find enclosing catch or try blocks
  clang::DynTypedNodeList Parents = Ctx.getParents(*Throw);
  llvm::SmallPtrSet<const void *, 16> Visited;

  // BFS through parent chain
  llvm::SmallVector<clang::DynTypedNode, 8> Worklist(Parents.begin(),
                                                      Parents.end());
  while (!Worklist.empty()) {
    auto Current = Worklist.pop_back_val();
    if (!Visited.insert(Current.getMemoizationData()).second)
      continue;

    if (Current.get<clang::CXXCatchStmt>()) {
      FoundCatch = true;
      break;
    }
    if (Current.get<clang::CXXTryStmt>())
      FoundTry = true;

    // Stop at function boundaries
    if (Current.get<clang::FunctionDecl>())
      break;

    for (const auto &P : Ctx.getParents(Current))
      Worklist.push_back(P);
  }

  if (FoundCatch) {
    emitVerified(Throw->getThrowLoc(), "rethrowNoCurrentException",
                 "bare throw is inside a catch block; rethrow is safe");
  } else if (FoundTry) {
    emitInconclusive(Throw->getThrowLoc(), "rethrowNoCurrentException",
                     "bare throw is in a try block but not directly in a catch; "
                     "safety depends on caller context");
  } else {
    emitContradiction(Throw->getThrowLoc(), "rethrowNoCurrentException",
                      "bare throw is not inside any catch block; "
                      "will call std::terminate if no active exception");
  }
}

} // namespace nix::tidy
