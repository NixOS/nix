#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "parser-tab.hh"

#include "lexer-tab.hh"

#include "eval.hh"
#include "filetransfer.hh"
#include "fetchers.hh"
#include "store-api.hh"
#include "flake/flake.hh"

namespace nix {

unsigned long Expr::nrExprs = 0;

Expr * EvalState::parse(
    char * text, size_t length, Pos::Origin origin, const SourcePath & basePath, std::shared_ptr<StaticEnv> & staticEnv)
{
    yyscan_t scanner;
    ParseData data{
        .state = *this,
        .symbols = symbols,
        .basePath = basePath,
        .origin = {origin},
    };

    yylex_init(&scanner);
    yy_scan_buffer(text, length, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);

    if (res)
        throw ParseError(data.error.value());

    data.result->bindVars(*this, staticEnv);

    return data.result;
}

SourcePath resolveExprPath(const SourcePath & path)
{
    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    auto path2 = path.resolveSymlinks();

    /* If `path' refers to a directory, append `/default.nix'. */
    if (path2.lstat().type == InputAccessor::tDirectory)
        return path2 + "default.nix";

    return path2;
}

Expr * EvalState::parseExprFromFile(const SourcePath & path)
{
    return parseExprFromFile(path, staticBaseEnv);
}

Expr * EvalState::parseExprFromFile(const SourcePath & path, std::shared_ptr<StaticEnv> & staticEnv)
{
    auto buffer = path.readFile();
    // readFile hopefully have left some extra space for terminators
    buffer.append("\0\0", 2);
    return parse(buffer.data(), buffer.size(), Pos::Origin(path), path.parent(), staticEnv);
}

Expr *
EvalState::parseExprFromString(std::string s_, const SourcePath & basePath, std::shared_ptr<StaticEnv> & staticEnv)
{
    auto s = make_ref<std::string>(std::move(s_));
    s->append("\0\0", 2);
    return parse(s->data(), s->size(), Pos::String{.source = s}, basePath, staticEnv);
}

Expr * EvalState::parseExprFromString(std::string s, const SourcePath & basePath)
{
    return parseExprFromString(std::move(s), basePath, staticBaseEnv);
}

Expr * EvalState::parseStdin()
{
    // Activity act(*logger, lvlTalkative, "parsing standard input");
    auto buffer = drainFD(0);
    // drainFD should have left some extra space for terminators
    buffer.append("\0\0", 2);
    auto s = make_ref<std::string>(std::move(buffer));
    return parse(s->data(), s->size(), Pos::Stdin{.source = s}, rootPath(CanonPath::fromCwd()), staticBaseEnv);
}

SourcePath EvalState::findFile(const std::string_view path)
{
    return findFile(searchPath, path);
}

SourcePath EvalState::findFile(const SearchPath & searchPath, const std::string_view path, const PosIdx pos)
{
    for (auto & i : searchPath.elements) {
        auto suffixOpt = i.prefix.suffixIfPotentialMatch(path);

        if (!suffixOpt)
            continue;
        auto suffix = *suffixOpt;

        auto rOpt = resolveSearchPathPath(i.path);
        if (!rOpt)
            continue;
        auto r = *rOpt;

        Path res = suffix == "" ? r : concatStrings(r, "/", suffix);
        if (pathExists(res))
            return CanonPath(canonPath(res));
    }

    if (hasPrefix(path, "nix/"))
        return CanonPath(concatStrings(corepkgsPrefix, path.substr(4)));

    debugThrow(
        ThrownError(
            {.msg = hintfmt(
                 evalSettings.pureEval
                     ? "cannot look up '<%s>' in pure evaluation mode (use '--impure' to override)"
                     : "file '%s' was not found in the Nix search path (add it using $NIX_PATH or -I)",
                 path),
             .errPos = positions[pos]}),
        0, 0);
}

std::optional<std::string> EvalState::resolveSearchPathPath(const SearchPath::Path & value0)
{
    auto & value = value0.s;
    auto i = searchPathResolved.find(value);
    if (i != searchPathResolved.end())
        return i->second;

    std::optional<std::string> res;

    if (EvalSettings::isPseudoUrl(value)) {
        try {
            auto storePath =
                fetchers::downloadTarball(store, EvalSettings::resolvePseudoUrl(value), "source", false).tree.storePath;
            res = {store->toRealPath(storePath)};
        } catch (FileTransferError & e) {
            logWarning({.msg = hintfmt("Nix search path entry '%1%' cannot be downloaded, ignoring", value)});
            res = std::nullopt;
        }
    }

    else if (hasPrefix(value, "flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        auto flakeRef = parseFlakeRef(value.substr(6), {}, true, false);
        debug("fetching flake search path element '%s''", value);
        auto storePath = flakeRef.resolve(store).fetchTree(store).first.storePath;
        res = {store->toRealPath(storePath)};
    }

    else {
        auto path = absPath(value);
        if (pathExists(path))
            res = {path};
        else {
            logWarning({.msg = hintfmt("Nix search path entry '%1%' does not exist, ignoring", value)});
            res = std::nullopt;
        }
    }

    if (res)
        debug("resolved search path element '%s' to '%s'", value, *res);
    else
        debug("failed to resolve search path element '%s'", value);

    searchPathResolved[value] = res;
    return res;
}

}
