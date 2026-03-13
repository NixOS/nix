#ifndef NIX_TIDY_VERIFY_DELETE_THIS_SAFETY_CHECK_H
#define NIX_TIDY_VERIFY_DELETE_THIS_SAFETY_CHECK_H

#include "VerifyCheckBase.h"

namespace nix::tidy {

/// Verifies that `delete this;` is not followed by any this-pointer access
/// in the enclosing function body.
///
/// Covers anomaly: semgrep-delete-this-eval-error (FP)
///
/// Logic:
///   - No post-delete this-access -> VERIFIED (safe move-out-then-delete)
///   - Post-delete this-access found -> CONTRADICTION (use-after-free)
class VerifyDeleteThisSafetyCheck : public VerifyCheckBase {
public:
  using VerifyCheckBase::VerifyCheckBase;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  /// Check if a statement contains any CXXThisExpr or implicit-this MemberExpr.
  static bool containsThisAccess(const clang::Stmt *S);
};

} // namespace nix::tidy

#endif
