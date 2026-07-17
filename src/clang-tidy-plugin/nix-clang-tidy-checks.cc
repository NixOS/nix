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

#include "nix-using-namespace.hh"

namespace nix::clang_tidy {

class NixClangTidyChecks : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories([[maybe_unused]] clang::tidy::ClangTidyCheckFactories & CheckFactories) override
    {
        CheckFactories.registerCheck<UsingNamespaceInNamespaceScopeCheck>("nix-using-namespace");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("nix-module", "Adds Nix-specific checks");

} // namespace nix::clang_tidy
