#include "eval.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return std::move(path);
}

}
