#include "eval.hh"
#include "fs-input-accessor.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

}
