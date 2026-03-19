#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::clang_tidy {

/// Flags throwing or catching exception types that don't derive from
/// nix::BaseError.
///
/// Nix's error-reporting machinery — traces, positions, hint formatting —
/// lives on nix::BaseError. Exceptions outside that hierarchy bypass all of
/// it and typically surface as bare "std::exception" lines with no context.
///
/// Allowed without a diagnostic:
///   - `catch (...)` — catch-all is fine, it's explicit about being broad
///   - `catch (BaseError &)` or any derived type
///   - `catch (std::bad_alloc &)` — wrapping it would allocate, which is
///     likely to fail for the same reason we're catching it
///   - `catch (std::filesystem::filesystem_error &)` — caught legitimately
///     at syscall boundaries and immediately converted to SysError with path
///     context; those conversion sites *are* the boundary
///   - bare `throw;` inside an allowed catch handler (rethrow)
///   - `throw E{...}` where E derives from BaseError
///
/// Also flags non-trivial `std::basic_regex` construction: it can throw
/// `std::regex_error`, which does not derive from BaseError. Callers should
/// wrap the constructor.
///
/// Ported from Lix's ForeignExceptions check, with BaseException→BaseError
/// and filesystem_error added to the catch allowlist.
class ForeignExceptionsCheck : public clang::tidy::ClangTidyCheck
{
public:
    using ClangTidyCheck::ClangTidyCheck;
    void registerMatchers(clang::ast_matchers::MatchFinder * Finder) override;
    void check(const clang::ast_matchers::MatchFinder::MatchResult & Result) override;
};

} // namespace nix::clang_tidy
