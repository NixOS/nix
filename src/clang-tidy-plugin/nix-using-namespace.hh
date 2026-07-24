#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::clang_tidy {

/**
 * Check that forbids instances on `using namespace ...;` in a namespace
 * scope.
 *
 * This is because we rely on unity builds in certain situations (faster
 * non-incremental builds, static initialiser issues), and the common pattern of
 * doing `using namespace` in a translation unit is a big footgun.
 *
 * Based on `google-build-using-namespace`, modulo that `using namespace` in a
 * non-namespace scope is fine (like in a function).
 */
class UsingNamespaceInNamespaceScopeCheck : public clang::tidy::ClangTidyCheck
{
public:
    UsingNamespaceInNamespaceScopeCheck(llvm::StringRef Name, clang::tidy::ClangTidyContext * Context)
        : ClangTidyCheck(Name, Context)
    {
    }

    bool isLanguageVersionSupported(const clang::LangOptions & LangOpts) const override
    {
        return LangOpts.CPlusPlus;
    }

    void registerMatchers(clang::ast_matchers::MatchFinder * Finder) override;

    void check(const clang::ast_matchers::MatchFinder::MatchResult & Result) override;
};

} // namespace nix::clang_tidy
