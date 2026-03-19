#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace nix::clang_tidy {

/// Flags `#include <iostream>` in header files (`.hh`).
///
/// Including <iostream> in a header drags the std::cout/cerr/clog static
/// initializers into every translation unit that transitively includes that
/// header, whether or not the streams are actually used. This bloats object
/// files and adds a small startup cost to every binary.
///
/// In headers, prefer:
///   - `<iosfwd>`  for forward declarations of std::ostream & friends
///   - `<ostream>` when you need the full std::ostream definition (e.g. for
///                 inline operator<< overloads)
///
/// `<iostream>` in .cc files is fine — the static-init cost is paid once per
/// TU that actually needs the streams.
///
/// Implemented as a preprocessor callback (no AST matching needed). Ported
/// from Lix's ForbiddenIncludes check, with the policy changed from "ban
/// nlohmann" to "ban <iostream> in headers".
class ForbiddenIncludesCheck : public clang::tidy::ClangTidyCheck
{
public:
    using ClangTidyCheck::ClangTidyCheck;
    void registerPPCallbacks(
        const clang::SourceManager & SM, clang::Preprocessor * PP, clang::Preprocessor * ModuleExpanderPP) override;
};

} // namespace nix::clang_tidy
