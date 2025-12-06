/**
 * @file NixClangTidyChecks.cc
 * @brief Custom clang-tidy checks for the Nix project.
 *
 * This module registers custom clang-tidy checks specific to the Nix codebase.
 * To add a new check:
 * 1. Create CheckName.hh and CheckName.cc in this directory
 * 2. Include the header here
 * 3. Register the check in addCheckFactories()
 * 4. Add the source file to meson.build
 * 5. Enable the check in .clang-tidy (e.g., nix-checkname)
 */

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::tidy;

class NixClangTidyChecks : public ClangTidyModule
{
public:
    void addCheckFactories([[maybe_unused]] ClangTidyCheckFactories & CheckFactories) override
    {
        // Custom checks will be registered here.
        // Example:
        // CheckFactories.registerCheck<MyCustomCheck>("nix-my-custom-check");
    }
};

static ClangTidyModuleRegistry::Add<NixClangTidyChecks> X("nix-module", "Adds Nix-specific checks");

} // namespace nix::clang_tidy
