#ifndef NIX_TIDY_DANGLING_CSTR_CHECK_H
#define NIX_TIDY_DANGLING_CSTR_CHECK_H

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::tidy {

/// Detects .c_str() or .data() called on a temporary std::string
/// whose result is stored in a const char* variable — a dangling
/// pointer since the temporary is destroyed at the semicolon.
///
///   const char *p = someString().c_str();  // bug: dangling
///   const char *p = (a + b).c_str();       // bug: dangling
///   std::string s = foo(); const char *p = s.c_str();  // ok
class DanglingCStrCheck : public clang::tidy::ClangTidyCheck
{
public:
    using ClangTidyCheck::ClangTidyCheck;

    void registerMatchers(clang::ast_matchers::MatchFinder * Finder) override;
    void check(const clang::ast_matchers::MatchFinder::MatchResult & Result) override;
};

} // namespace nix::tidy

#endif
