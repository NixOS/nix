#ifndef NIX_TIDY_LOCK_GUARD_TEMPORARY_CHECK_H
#define NIX_TIDY_LOCK_GUARD_TEMPORARY_CHECK_H

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::tidy {

/// Detects std::lock_guard (and std::scoped_lock, std::unique_lock)
/// constructed as temporaries — immediately destroyed, providing no
/// actual synchronisation.
///
///   std::lock_guard<std::mutex>(mtx);  // bug: temporary, no lock held
///   std::lock_guard<std::mutex> lock(mtx);  // ok: named variable
class LockGuardTemporaryCheck : public clang::tidy::ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace nix::tidy

#endif
