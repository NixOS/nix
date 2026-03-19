/**
 * @brief Custom clang-tidy checks for the Nix project.
 *
 * This module registers custom clang-tidy checks specific to the Nix codebase.
 * To add a new check:
 * 1. Create check-name.hh and check-name.cc in this directory
 * 2. Include the header here
 * 3. Register the check in addCheckFactories()
 * 4. Add the source file to meson.build
 * 5. Enable the check in .clang-tidy (e.g., nix-checkname)
 */

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

#include "forbidden-includes.hh"
#include "foreign-exceptions.hh"
#include "format-string-arity.hh"

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;

class NixClangTidyChecks : public ClangTidyModule
{
public:
    void addCheckFactories(ClangTidyCheckFactories & CheckFactories) override
    {
        CheckFactories.registerCheck<ForbiddenIncludesCheck>("nix-forbidden-includes");
        CheckFactories.registerCheck<ForeignExceptionsCheck>("nix-foreign-exceptions");
        CheckFactories.registerCheck<FormatStringArityCheck>("nix-format-string-arity");
    }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("nix-module", "Adds Nix-specific checks");

} // namespace nix::clang_tidy
