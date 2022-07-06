#include "eval.hh"
#include "util.hh"

namespace nix {

SourcePath EvalState::rootPath(const Path & path)
{
    return {rootFS, CanonPath(path)};
}

void EvalState::registerAccessor(ref<InputAccessor> accessor)
{
    inputAccessors.emplace(&*accessor, accessor);
}

}
