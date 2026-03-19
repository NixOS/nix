#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::clang_tidy {

/// Checks that boost::format-style format strings passed to nix::fmt() and
/// nix::BaseError-derived exception constructors have a matching number of
/// arguments for their placeholders.
///
/// nix::fmt() explicitly disables boost::format's too_few_args_bit and
/// too_many_args_bit exceptions (see src/libutil/include/nix/util/fmt.hh), so
/// arity mismatches are completely silent at runtime. Worse, with zero extra
/// arguments the single-argument overloads of fmt()/HintFmt() are selected,
/// which treat the string as a literal — placeholders like '%s' appear verbatim
/// in the output.
///
/// Matches:
///   - Constructors of nix::BaseError and all derived exception classes
///     (Error, SysError, UsageError, and all MakeError(...)-declared types)
///   - nix::fmt() calls (covers printError/printInfo/debug/etc. macros,
///     which all expand to fmt())
///
/// Only fires when the format string is a string literal — dynamic format
/// strings can't be checked at compile time.
class FormatStringArityCheck : public clang::tidy::ClangTidyCheck
{
public:
    using ClangTidyCheck::ClangTidyCheck;
    void registerMatchers(clang::ast_matchers::MatchFinder * Finder) override;
    void check(const clang::ast_matchers::MatchFinder::MatchResult & Result) override;
};

} // namespace nix::clang_tidy
