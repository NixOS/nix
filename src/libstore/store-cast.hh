#pragma once

#include "store-api.hh"

namespace nix {

template<typename T>
T & require(Store & store)
{
    auto * castedStore = dynamic_cast<T *>(&store);
    if (!castedStore)
        throw UsageError("%s not supported by store '%s'", T::operationName, store.getUri());
    return *castedStore;
}

}
