#pragma once

#include "source-accessor.hh"

namespace nix {

struct MountedSourceAccessor : SourceAccessor
{
    virtual void mount(CanonPath mountPoint, ref<SourceAccessor> accessor) = 0;

    /**
     * Return the accessor mounted on `mountPoint`, or `nullptr` if
     * there is no such mount point.
     */
    virtual std::shared_ptr<SourceAccessor> getMount(CanonPath mountPoint) = 0;
};

ref<MountedSourceAccessor> makeMountedSourceAccessor(std::map<CanonPath, ref<SourceAccessor>> mounts);

} // namespace nix
