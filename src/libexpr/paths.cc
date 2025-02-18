#include "eval.hh"
#include "store-api.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

SourcePath EvalState::rootPath(PathView path)
{
    return {rootFS, CanonPath(absPath(path))};
}

SourcePath EvalState::stringWithContextToPath(std::string_view s, const NixStringContext & context)
{
    auto path = CanonPath(s);
    return !context.empty() ? SourcePath{storeFS, std::move(path)} : rootPath(std::move(path));
}

}
