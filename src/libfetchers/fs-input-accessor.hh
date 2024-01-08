#pragma once

#include "input-accessor.hh"

namespace nix {

class StorePath;
class Store;

ref<InputAccessor> makeFSInputAccessor(
    const CanonPath & root);

ref<InputAccessor> makeStorePathAccessor(
    ref<Store> store,
    const StorePath & storePath);

SourcePath getUnfilteredRootPath(CanonPath path);

}
