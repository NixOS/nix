#include "forbidden-includes.hh"

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

namespace nix::clang_tidy {

using namespace clang;

namespace {

/// PPCallbacks hook that fires on every `#include` directive during
/// preprocessing. Unlike the Lix original, we emit diagnostics directly here
/// (via a back-reference to the check) instead of collecting marks and
/// replaying them in check() — this is the pattern upstream clang-tidy uses
/// for pure-preprocessor checks (see e.g. llvm-include-order).
class ForbiddenIncludesPPCallbacks : public PPCallbacks
{
    ForbiddenIncludesCheck & Check;
    const SourceManager & SM;

public:
    ForbiddenIncludesPPCallbacks(ForbiddenIncludesCheck & Check, const SourceManager & SM)
        : Check(Check)
        , SM(SM)
    {
    }

    void InclusionDirective(
        SourceLocation HashLoc,
        const Token & /*IncludeTok*/,
        StringRef FileName,
        bool /*IsAngled*/,
        CharSourceRange FilenameRange,
        OptionalFileEntryRef /*File*/,
        StringRef /*SearchPath*/,
        StringRef /*RelativePath*/,
        const Module * /*SuggestedModule*/,
        bool /*ModuleImported*/,
        SrcMgr::CharacteristicKind /*FileType*/) override
    {
        if (FileName != "iostream")
            return;

        // Don't flag includes that live inside system headers (e.g. some
        // boost header that itself includes <iostream>). HashLoc points at
        // the '#' in the includer, so its file characteristic is the
        // includer's, not the includee's.
        if (SM.getFileCharacteristic(HashLoc) != SrcMgr::C_User)
            return;

        // Only flag when the includer is a .hh file. <iostream> in a .cc
        // file is fine — the cost is paid once, locally.
        StringRef includer = SM.getFilename(HashLoc);
        if (!includer.ends_with(".hh"))
            return;

        Check.diag(
            HashLoc,
            "including <iostream> in a header pulls in std::cout/cerr static "
            "initializers into every TU; use <iosfwd> or <ostream> instead")
            << FilenameRange;
    }
};

} // anonymous namespace

void ForbiddenIncludesCheck::registerPPCallbacks(
    const SourceManager & SM, Preprocessor * PP, Preprocessor * /*ModuleExpanderPP*/)
{
    PP->addPPCallbacks(std::make_unique<ForbiddenIncludesPPCallbacks>(*this, SM));
}

} // namespace nix::clang_tidy
