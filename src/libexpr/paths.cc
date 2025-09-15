#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

SourcePath EvalState::rootPath(PathView path)
{
    return {rootFS, CanonPath(absPath(path))};
}

SourcePath EvalState::storePath(const StorePath & path)
{
    return {rootFS, CanonPath{store->printStorePath(path)}};
}

} // namespace nix
