#include "eval.hh"
#include "util.hh"

namespace nix {

SourcePath EvalState::rootPath(Path path)
{
    return {*rootFS, std::move(path)};
}

InputAccessor & EvalState::registerAccessor(ref<InputAccessor> accessor)
{
    inputAccessors.emplace(&*accessor, accessor);
    return *accessor;
}

}
