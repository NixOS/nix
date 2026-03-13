#ifndef NIX_TIDY_VERIFY_CONSTRAINED_CTOR_CHECK_H
#define NIX_TIDY_VERIFY_CONSTRAINED_CTOR_CHECK_H

#include "VerifyCheckBase.h"

namespace nix::tidy {

/// Verifies that constructors with forwarding-reference-like parameters
/// have C++20 requires clauses to prevent overload hijacking.
///
/// Covers anomaly: clang-tidy-forwarding-ref-overload-error (FP)
///
/// Logic:
///   - Has trailing requires clause -> VERIFIED (constraint prevents hijacking)
///   - No constraint -> CONTRADICTION (forwarding ref may hijack)
class VerifyConstrainedCtorCheck : public VerifyCheckBase {
public:
  using VerifyCheckBase::VerifyCheckBase;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace nix::tidy

#endif
