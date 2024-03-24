#include "input-accessor.hh"
#include "source-path.hh"

namespace nix {

/**
 * An input accessor for an in-memory file system.
 */
struct MemoryInputAccessor : InputAccessor
{
    virtual SourcePath addFile(CanonPath path, std::string && contents) = 0;
};

ref<MemoryInputAccessor> makeMemoryInputAccessor();

ref<InputAccessor> makeEmptyInputAccessor();

}
