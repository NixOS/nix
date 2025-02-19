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

}
