#ifndef NIX_TIDY_SQLITE_NON_LITERAL_CHECK_H
#define NIX_TIDY_SQLITE_NON_LITERAL_CHECK_H

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::tidy {

/// Detects calls to sqlite3_exec() where the SQL string argument is
/// not a string literal (or constexpr/static const variable).
/// Non-literal SQL in sqlite3_exec() is a SQL injection risk.
class SqliteNonLiteralCheck : public clang::tidy::ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;

  void registerMatchers(clang::ast_matchers::MatchFinder *Finder) override;
  void check(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  bool isSafeExpression(const clang::Expr *E) const;
};

} // namespace nix::tidy

#endif
