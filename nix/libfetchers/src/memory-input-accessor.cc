#include "memory-input-accessor.hh"
#include "memory-source-accessor.hh"
#include "source-path.hh"

namespace nix {

struct MemoryInputAccessorImpl : MemoryInputAccessor, MemorySourceAccessor
{
    SourcePath addFile(CanonPath path, std::string && contents) override
    {
        return {
            ref(shared_from_this()),
            MemorySourceAccessor::addFile(path, std::move(contents))
        };
    }
};

ref<MemoryInputAccessor> makeMemoryInputAccessor()
{
    return make_ref<MemoryInputAccessorImpl>();
}

ref<InputAccessor> makeEmptyInputAccessor()
{
    static auto empty = makeMemoryInputAccessor().cast<InputAccessor>();
    return empty;
}

}
