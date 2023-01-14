#pragma once

#include "comparator.hh"

#include <set>

namespace nix {

template<typename Ref>
struct References
{
    std::set<Ref> others;
    bool self = false;

    bool empty() const;
    size_t size() const;

    /* Functions to view references + self as one set, mainly for
       compatibility's sake. */
    std::set<Ref> possiblyToSelf(const Ref & self) const;
    void insertPossiblyToSelf(const Ref & self, Ref && ref);
    void setPossiblyToSelf(const Ref & self, std::set<Ref> && refs);

    GENERATE_CMP(References<Ref>, me->others, me->self);
};

template<typename Ref>
bool References<Ref>::empty() const
{
    return !self && others.empty();
}

template<typename Ref>
size_t References<Ref>::size() const
{
    return (self ? 1 : 0) + others.size();
}

template<typename Ref>
std::set<Ref> References<Ref>::possiblyToSelf(const Ref & selfRef) const
{
    std::set<Ref> refs { others };
    if (self)
        refs.insert(selfRef);
    return refs;
}

template<typename Ref>
void References<Ref>::insertPossiblyToSelf(const Ref & selfRef, Ref && ref)
{
    if (ref == selfRef)
        self = true;
    else
        others.insert(std::move(ref));
}

template<typename Ref>
void References<Ref>::setPossiblyToSelf(const Ref & selfRef, std::set<Ref> && refs)
{
    if (refs.count(selfRef)) {
        self = true;
        refs.erase(selfRef);
    }

    others = refs;
}

}
