#pragma once

#include "input-accessor.hh"

namespace nix {

class StorePath;
class Store;

struct FSInputAccessor : InputAccessor
{
    virtual void checkAllowed(const CanonPath & absPath) = 0;

    virtual void allowPath(CanonPath path) = 0;

    virtual bool hasAccessControl() = 0;
};

ref<FSInputAccessor> makeFSInputAccessor(
    const CanonPath & root,
    std::optional<std::set<CanonPath>> && allowedPaths = {},
    MakeNotAllowedError && makeNotAllowedError = {});

ref<FSInputAccessor> makeStorePathAccessor(
    ref<Store> store,
    const StorePath & storePath,
    MakeNotAllowedError && makeNotAllowedError = {});

SourcePath getUnfilteredRootPath(CanonPath path);

}
