#ifndef NIX_TIDY_VERIFY_RETHROW_CONTEXT_CHECK_H
#define NIX_TIDY_VERIFY_RETHROW_CONTEXT_CHECK_H

#include "VerifyCheckBase.h"

namespace nix::tidy {

/// Verifies bare `throw;` statements are safely nested inside catch blocks.
///
/// Covers anomalies: cppcheck-rethrow-util (FP), cppcheck-rethrow-nix-api-util (FP)
///
/// Logic:
///   - CXXCatchStmt ancestor found -> VERIFIED (rethrow guaranteed safe)
///   - Only CXXTryStmt ancestors, no catch -> INCONCLUSIVE (depends on caller)
///   - No try/catch at all -> CONTRADICTION (will call std::terminate)
class VerifyRethrowContextCheck : public VerifyCheckBase {
public:
  using VerifyCheckBase::VerifyCheckBase;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace nix::tidy

#endif
