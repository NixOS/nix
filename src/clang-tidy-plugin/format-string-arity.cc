#include "format-string-arity.hh"

#include <clang/AST/ASTContext.h>
#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <cctype>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::ast_matchers;

namespace {

struct PlaceholderInfo
{
    unsigned sequential = 0;    ///< count of %s, %d, %x, etc.
    unsigned maxPositional = 0; ///< highest N seen in %N% (1-indexed)
};

/// Count boost::format placeholders in a format string.
///
/// Handles:
///   - `%%`   → literal percent, not a placeholder
///   - `%N%`  → positional (boost-style); track the highest N
///   - `%...` → anything else starting with % is a sequential placeholder
///              (%s, %d, %x, %|spec|, %-5.2f, etc.)
///
/// We don't need to parse the full printf spec — we only need the count.
PlaceholderInfo countPlaceholders(llvm::StringRef fmt)
{
    PlaceholderInfo info;
    size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != '%') {
            ++i;
            continue;
        }
        ++i;
        if (i >= fmt.size())
            break; // trailing '%', malformed — let boost deal with it
        if (fmt[i] == '%') {
            ++i;
            continue; // "%%" → literal percent
        }

        // Positional: "%N%" where N is decimal digits
        if (std::isdigit(static_cast<unsigned char>(fmt[i]))) {
            unsigned n = 0;
            size_t j = i;
            while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) {
                n = n * 10 + static_cast<unsigned>(fmt[j] - '0');
                ++j;
            }
            if (j < fmt.size() && fmt[j] == '%') {
                info.maxPositional = std::max(info.maxPositional, n);
                i = j + 1;
                continue;
            }
            // Not positional (e.g. "%5d" — width spec); fall through.
        }

        ++info.sequential;
        // Advance past the conversion character. Under-advancing is harmless:
        // the next iteration just skips non-'%' chars.
        ++i;
    }
    return info;
}

/// Unwrap a string literal that may be buried under implicit std::string
/// construction. When a `const char[N]` is passed to a `const std::string &`
/// parameter, clang generates:
///
///   MaterializeTemporaryExpr
///     CXXBindTemporaryExpr
///       CXXConstructExpr std::basic_string(const char *, allocator)
///         ImplicitCastExpr <ArrayToPointerDecay>
///           StringLiteral
///
/// For `const char *` parameters the chain is just ImplicitCastExpr →
/// StringLiteral, which IgnoreParenImpCasts() already handles.
const StringLiteral * unwrapStringLiteral(const Expr * e)
{
    e = e->IgnoreParenImpCasts();
    if (const auto * mte = dyn_cast<MaterializeTemporaryExpr>(e))
        e = mte->getSubExpr()->IgnoreParenImpCasts();
    if (const auto * bte = dyn_cast<CXXBindTemporaryExpr>(e))
        e = bte->getSubExpr()->IgnoreParenImpCasts();
    if (const auto * ce = dyn_cast<CXXConstructExpr>(e)) {
        // Only follow through std::basic_string implicit construction — don't
        // accidentally unwrap through user-defined conversions.
        const auto * rd = ce->getType()->getAsCXXRecordDecl();
        if (rd && rd->getName() == "basic_string" && ce->getNumArgs() >= 1)
            e = ce->getArg(0)->IgnoreParenImpCasts();
    }
    return dyn_cast<StringLiteral>(e);
}

} // anonymous namespace

void FormatStringArityCheck::registerMatchers(MatchFinder * Finder)
{
    // Constructing any exception derived from nix::BaseError. The format-
    // string overload BaseError(const std::string & fs, Args &&...) is by far
    // the most common path; it's inherited by every MakeError()-declared type
    // (Error, SysError, UsageError, ...).
    //
    // Matching the ctor call broadly and extracting the string literal in
    // check() lets us look through implicit std::string construction, which
    // the hasArgument(stringLiteral()) matcher alone can't.
    Finder->addMatcher(
        cxxConstructExpr(
            hasDeclaration(cxxConstructorDecl(ofClass(cxxRecordDecl(isSameOrDerivedFrom("::nix::BaseError"))))),
            argumentCountAtLeast(1),
            unless(isInTemplateInstantiation()))
            .bind("ctor"),
        this);

    // nix::fmt("...", ...) — used directly and by the printError/printInfo/
    // debug/notice/vomit macros, which all expand to fmt(args).
    Finder->addMatcher(
        callExpr(
            callee(functionDecl(hasName("::nix::fmt"))), argumentCountAtLeast(1), unless(isInTemplateInstantiation()))
            .bind("call"),
        this);
}

void FormatStringArityCheck::check(const MatchFinder::MatchResult & Result)
{
    unsigned numArgs;
    const Expr * arg0;
    SourceLocation reportLoc;

    if (const auto * ctor = Result.Nodes.getNodeAs<CXXConstructExpr>("ctor")) {
        numArgs = ctor->getNumArgs();
        arg0 = ctor->getArg(0);
        reportLoc = ctor->getBeginLoc();
    } else if (const auto * call = Result.Nodes.getNodeAs<CallExpr>("call")) {
        numArgs = call->getNumArgs();
        arg0 = call->getArg(0);
        reportLoc = call->getBeginLoc();
    } else {
        return;
    }

    const StringLiteral * fmtLit = unwrapStringLiteral(arg0);
    if (!fmtLit || !fmtLit->isOrdinary())
        return;

    auto info = countPlaceholders(fmtLit->getString());

    // Mixing positional (%1%) with sequential (%s) is unusual in boost::format
    // and the required arg count is ambiguous. Don't try to validate.
    if (info.sequential > 0 && info.maxPositional > 0)
        return;

    unsigned expected = std::max(info.sequential, info.maxPositional);
    if (expected == 0)
        return; // no placeholders, nothing to check

    // Format string is at arg index 0; format arguments follow.
    unsigned supplied = numArgs - 1;

    if (supplied == expected)
        return;

    diag(reportLoc, "format string has %0 placeholder%s0 but %1 argument%s1 supplied")
        << expected << supplied << fmtLit->getSourceRange();

    if (supplied == 0)
        diag(
            fmtLit->getBeginLoc(),
            "with zero arguments the single-parameter overload is selected, "
            "which treats the string as a literal; '%%s' will appear verbatim",
            DiagnosticIDs::Note);
}

} // namespace nix::clang_tidy
