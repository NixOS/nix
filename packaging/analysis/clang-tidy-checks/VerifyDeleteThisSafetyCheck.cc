#include "VerifyDeleteThisSafetyCheck.h"

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

namespace {

/// Visitor that checks if a subtree contains CXXThisExpr.
class ThisAccessFinder : public clang::RecursiveASTVisitor<ThisAccessFinder> {
public:
  bool Found = false;

  bool VisitCXXThisExpr(clang::CXXThisExpr *) {
    Found = true;
    return false; // stop traversal
  }
};

} // namespace

bool VerifyDeleteThisSafetyCheck::containsThisAccess(const clang::Stmt *S) {
  if (!S)
    return false;
  ThisAccessFinder Finder;
  // RecursiveASTVisitor needs a non-const Stmt*
  Finder.TraverseStmt(const_cast<clang::Stmt *>(S));
  return Finder.Found;
}

void VerifyDeleteThisSafetyCheck::registerMatchers(MatchFinder *Finder) {
  // Match: delete this;
  Finder->addMatcher(
      cxxDeleteExpr(has(ignoringImplicit(cxxThisExpr())),
                    hasAncestor(functionDecl().bind("func")))
          .bind("delete"),
      this);
}

void VerifyDeleteThisSafetyCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Delete = Result.Nodes.getNodeAs<clang::CXXDeleteExpr>("delete");
  const auto *Func = Result.Nodes.getNodeAs<clang::FunctionDecl>("func");
  if (!Delete || !Func || !Func->getBody())
    return;

  const auto *Body = llvm::dyn_cast<clang::CompoundStmt>(Func->getBody());
  if (!Body)
    return;

  auto &SM = *Result.SourceManager;
  clang::SourceLocation DeleteLoc = Delete->getBeginLoc();

  // Find all statements after the delete this; in the function body.
  bool PastDelete = false;
  bool FoundPostDeleteThisAccess = false;

  for (const auto *Child : Body->body()) {
    if (PastDelete) {
      if (containsThisAccess(Child)) {
        FoundPostDeleteThisAccess = true;
        break;
      }
    } else if (Child == Delete) {
      PastDelete = true;
    } else {
      // The delete might be nested in an expression statement
      // Compare source locations: if Child starts after Delete, we're past it
      if (SM.isBeforeInTranslationUnit(DeleteLoc, Child->getBeginLoc())) {
        // Check if this child contains our delete expression
        // If not, and it's after, then we're past the delete
      }

      // Try to find the delete within this child (e.g., ExprStmt wrapping delete)
      for (auto It = Child->child_begin(); It != Child->child_end(); ++It) {
        if (*It == Delete) {
          PastDelete = true;
          break;
        }
      }
    }
  }

  // Also handle the case where delete is wrapped in an ExprStmt:
  // walk the body statements by source location order
  if (!PastDelete) {
    // Fall back to source-location-based ordering
    PastDelete = false;
    for (const auto *Child : Body->body()) {
      if (PastDelete) {
        if (containsThisAccess(Child)) {
          FoundPostDeleteThisAccess = true;
          break;
        }
      }
      if (SM.isBeforeInTranslationUnit(DeleteLoc, Child->getEndLoc()) &&
          !SM.isBeforeInTranslationUnit(DeleteLoc, Child->getBeginLoc())) {
        // Delete is inside this statement
        PastDelete = true;
      }
    }
  }

  if (FoundPostDeleteThisAccess) {
    emitContradiction(Delete->getBeginLoc(), "deleteThis",
                      "this-pointer is accessed after delete this; "
                      "potential use-after-free");
  } else {
    emitVerified(Delete->getBeginLoc(), "deleteThis",
                 "no this-pointer access after delete this; "
                 "safe move-out-then-delete pattern");
  }
}

} // namespace nix::tidy
