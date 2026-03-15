#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

#include "DanglingCStrCheck.h"
#include "LockGuardTemporaryCheck.h"
#include "SqliteNonLiteralCheck.h"
#include "VerifyRethrowContextCheck.h"
#include "VerifyConstrainedCtorCheck.h"
#include "VerifyMoveFromFlagCheck.h"
#include "VerifyDeleteThisSafetyCheck.h"

namespace nix::tidy {

class NixTidyModule : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories & CheckFactories) override
    {
        CheckFactories.registerCheck<DanglingCStrCheck>("nix-dangling-cstr");
        CheckFactories.registerCheck<LockGuardTemporaryCheck>("nix-lock-guard-temporary");
        CheckFactories.registerCheck<SqliteNonLiteralCheck>("nix-sqlite-non-literal-sql");
        CheckFactories.registerCheck<VerifyRethrowContextCheck>("nix-verify-rethrow-context");
        CheckFactories.registerCheck<VerifyConstrainedCtorCheck>("nix-verify-constrained-ctor");
        CheckFactories.registerCheck<VerifyMoveFromFlagCheck>("nix-verify-move-from-flag");
        CheckFactories.registerCheck<VerifyDeleteThisSafetyCheck>("nix-verify-delete-this-safety");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<NixTidyModule>
    X("nix-module", "Adds Nix project-specific clang-tidy checks.");

// Force the linker to keep the module registration.
volatile int NixTidyModuleAnchorSource = 0;

} // namespace nix::tidy
