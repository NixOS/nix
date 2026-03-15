#include "VerifyMoveFromFlagCheck.h"

#include <clang/AST/DeclCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>

using namespace clang::ast_matchers;

namespace nix::tidy {

void VerifyMoveFromFlagCheck::registerMatchers(MatchFinder *Finder) {
  // Match: inside a move constructor body, a binary operator assigning
  // to a member of the rvalue-reference parameter.
  //
  // Pattern: other.movedFrom = true;
  //   binaryOperator(=)
  //     LHS: memberExpr on declRefExpr(to move ctor param)
  //     RHS: (some value)
  Finder->addMatcher(
      binaryOperator(
          isAssignmentOperator(),
          hasLHS(memberExpr(
              hasObjectExpression(
                  declRefExpr(to(parmVarDecl(hasType(rValueReferenceType()))
                                     .bind("param"))))
          ).bind("member")),
          hasAncestor(cxxConstructorDecl(isMoveConstructor()).bind("moveCtor")))
          .bind("assign"),
      this);
}

void VerifyMoveFromFlagCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Assign = Result.Nodes.getNodeAs<clang::BinaryOperator>("assign");
  const auto *Member = Result.Nodes.getNodeAs<clang::MemberExpr>("member");
  if (!Assign || !Member)
    return;

  const auto *Field = llvm::dyn_cast<clang::FieldDecl>(Member->getMemberDecl());
  if (!Field)
    return;

  if (Field->getType()->isBooleanType()) {
    emitVerified(Assign->getOperatorLoc(), "accessMoved",
                 "assignment to bool member of moved-from parameter is the "
                 "intentional moved-from tracking flag pattern");
  } else {
    emitContradiction(Assign->getOperatorLoc(), "accessMoved",
                      "assignment to non-bool member of moved-from parameter "
                      "in move constructor; potential use-after-move");
  }
}

} // namespace nix::tidy
