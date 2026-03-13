#include "VerifyCheckBase.h"

namespace nix::tidy {

void VerifyCheckBase::emitVerified(clang::SourceLocation Loc,
                                   llvm::StringRef OriginalCheckId,
                                   llvm::StringRef Explanation) {
  diag(Loc, "[VERIFIED:%0] %1", clang::DiagnosticIDs::Note)
      << OriginalCheckId << Explanation;
}

void VerifyCheckBase::emitContradiction(clang::SourceLocation Loc,
                                        llvm::StringRef OriginalCheckId,
                                        llvm::StringRef Explanation) {
  diag(Loc, "[CONTRADICTION:%0] %1", clang::DiagnosticIDs::Warning)
      << OriginalCheckId << Explanation;
}

void VerifyCheckBase::emitInconclusive(clang::SourceLocation Loc,
                                       llvm::StringRef OriginalCheckId,
                                       llvm::StringRef Explanation) {
  diag(Loc, "[INCONCLUSIVE:%0] %1", clang::DiagnosticIDs::Note)
      << OriginalCheckId << Explanation;
}

} // namespace nix::tidy
