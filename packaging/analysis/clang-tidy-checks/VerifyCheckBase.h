#ifndef NIX_TIDY_VERIFY_CHECK_BASE_H
#define NIX_TIDY_VERIFY_CHECK_BASE_H

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::tidy {

/// Base class for AST-based anomaly verification checks.
///
/// Emits structured diagnostics at note (VERIFIED/INCONCLUSIVE) or
/// warning (CONTRADICTION) level for cross-referencing with anomalies.toml.
///
/// Diagnostic format:
///   file:line:col: note: [VERIFIED:checkId] explanation [nix-verify-name]
///   file:line:col: warning: [CONTRADICTION:checkId] explanation [nix-verify-name]
///   file:line:col: note: [INCONCLUSIVE:checkId] explanation [nix-verify-name]
class VerifyCheckBase : public clang::tidy::ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;

protected:
  /// Emit a VERIFIED note — the false-positive classification is confirmed.
  void emitVerified(clang::SourceLocation Loc,
                    llvm::StringRef OriginalCheckId,
                    llvm::StringRef Explanation);

  /// Emit a CONTRADICTION warning — the classification appears incorrect.
  void emitContradiction(clang::SourceLocation Loc,
                         llvm::StringRef OriginalCheckId,
                         llvm::StringRef Explanation);

  /// Emit an INCONCLUSIVE note — cannot confirm or deny.
  void emitInconclusive(clang::SourceLocation Loc,
                        llvm::StringRef OriginalCheckId,
                        llvm::StringRef Explanation);
};

} // namespace nix::tidy

#endif
