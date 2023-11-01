#pragma once
///@file


#include <set>
#include <tuple>
#include "comparator.hh"

namespace nix {
template<typename AccessControlEntity>
struct AccessStatusFor {
    bool isProtected = false;
    std::set<AccessControlEntity> entities;

    GENERATE_CMP(AccessStatusFor<AccessControlEntity>, me->isProtected, me->entities);
};
}

