#pragma once

namespace nix {

template<typename T> class BasicStore;
class StoreConfig;
typedef BasicStore<StoreConfig> Store;

}
