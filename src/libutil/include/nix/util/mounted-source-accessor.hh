#pragma once

#include "source-accessor.hh"

namespace nix {

struct MountedSourceAccessor : SourceAccessor
{
    virtual void mount(CanonPath mountPoint, ref<SourceAccessor> accessor) = 0;
};

ref<MountedSourceAccessor> makeMountedSourceAccessor(std::map<CanonPath, ref<SourceAccessor>> mounts);

}
