#ifndef NIX_TIDY_VERIFY_MOVE_FROM_FLAG_CHECK_H
#define NIX_TIDY_VERIFY_MOVE_FROM_FLAG_CHECK_H

#include "VerifyCheckBase.h"

namespace nix::tidy {

/// Verifies that member access on a moved-from parameter inside a move
/// constructor is the intentional bool-flag pattern (setting movedFrom = true).
///
/// Covers anomalies: cppcheck-accessMoved-finally-ctor (FP),
///                   cppcheck-accessMoved-finally-dtor (FP)
///
/// Logic:
///   - Assignment to a bool member of the moved-from param -> VERIFIED
///   - Non-bool member access after move -> CONTRADICTION
class VerifyMoveFromFlagCheck : public VerifyCheckBase {
public:
  using VerifyCheckBase::VerifyCheckBase;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace nix::tidy

#endif
