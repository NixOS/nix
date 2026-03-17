#include "foreign-exceptions.hh"

#include <clang/AST/ExprCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <llvm/Support/ErrorHandling.h>

namespace nix::clang_tidy {

using namespace clang;
using namespace clang::ast_matchers;

void ForeignExceptionsCheck::registerMatchers(MatchFinder * Finder)
{
    // Exception types that may be thrown/caught without wrapping.
    //
    // bad_alloc: wrapping would require allocating — if we're already OOM,
    // the wrap itself will likely fail.
    //
    // filesystem_error: caught at std::filesystem call sites and immediately
    // converted to SysError with path context (see libutil/file-system.cc).
    // Those catch sites are the wrapping boundary.
    auto allowedExceptions = cxxRecordDecl(anyOf(
        isSameOrDerivedFrom(hasName("::nix::BaseError")),
        hasName("::std::bad_alloc"),
        hasName("::std::filesystem::filesystem_error")));

    auto isAllowedCatch = anyOf(isCatchAll(), has(varDecl(hasType(references(allowedExceptions)))));

    Finder->addMatcher(traverse(TK_AsIs, cxxCatchStmt(unless(isAllowedCatch)).bind("catch")), this);

    // Bare `throw;` is allowed when lexically inside a catch handler for an
    // allowed type. forCallable(equalsBoundNode("fn")) scopes the ancestor
    // walk to the current function so we don't match a rethrow in a lambda
    // against an enclosing function's catch.
    auto rethrowsAllowed =
        allOf(unless(has(expr())), hasAncestor(stmt(forCallable(equalsBoundNode("fn")), cxxCatchStmt(isAllowedCatch))));

    // `throw e` is allowed if e's type derives from BaseError. Two shapes:
    //   - `throw err;` where err is already typed as an allowed exception
    //   - `throw Error(...)` where the throw operand is a CXXConstructExpr
    //     whose constructor belongs to an allowed record
    auto throwsAllowed = anyOf(
        has(expr(hasType(allowedExceptions))), has(cxxConstructExpr(hasDeclaration(hasParent(allowedExceptions)))));

    Finder->addMatcher(
        traverse(
            TK_AsIs,
            stmt(
                forCallable(decl().bind("fn")),
                cxxThrowExpr(unless(anyOf(rethrowsAllowed, throwsAllowed))).bind("throw"))),
        this);

    // std::regex constructors (other than default/copy/move) parse the
    // pattern and throw std::regex_error on bad syntax. regex_error is a
    // std::runtime_error, not a BaseError — flag so callers wrap it.
    Finder->addMatcher(
        traverse(
            TK_AsIs,
            cxxConstructExpr(hasDeclaration(cxxConstructorDecl(
                                 hasAncestor(cxxRecordDecl(hasName("::std::basic_regex"))),
                                 unless(anyOf(isDefaultConstructor(), isCopyConstructor(), isMoveConstructor())))))
                .bind("bad-ctor")),
        this);
}

void ForeignExceptionsCheck::check(const MatchFinder::MatchResult & Result)
{
    if (const auto * catchStmt = Result.Nodes.getNodeAs<CXXCatchStmt>("catch")) {
        diag(
            catchStmt->getCatchLoc(),
            "catching a foreign exception type that doesn't derive from "
            "nix::BaseError; wrap in nix::Error or catch a narrower type");
    } else if (const auto * throwExpr = Result.Nodes.getNodeAs<CXXThrowExpr>("throw")) {
        if (throwExpr->getSubExpr() && throwExpr->getSubExpr()->isTypeDependent()) {
            diag(
                throwExpr->getThrowLoc(),
                "thrown exception is type-dependent; ensure the template "
                "argument derives from nix::BaseError and NOLINT this site");
        } else {
            diag(
                throwExpr->getThrowLoc(),
                "throwing a foreign exception type that doesn't derive from "
                "nix::BaseError; wrap in nix::Error or a derived type");
        }
    } else if (const auto * ctor = Result.Nodes.getNodeAs<CXXConstructExpr>("bad-ctor")) {
        diag(
            ctor->getLocation(),
            "%0 can throw std::regex_error which does not derive from "
            "nix::BaseError; wrap the constructor in a try/catch")
            << ctor->getConstructor()->getNameAsString();
    } else {
        llvm_unreachable("unhandled match");
    }
}

} // namespace nix::clang_tidy
