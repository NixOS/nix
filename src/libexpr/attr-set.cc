#include "attr-set.hh"
#include "eval-inline.hh"

#include <algorithm>


namespace nix {


/* Allocate a new array of attributes for an attribute set with a
   specific capacity. The space is implicitly reserved after the
   Bindings structure. */
Ptr<Bindings> Bindings::allocBindings(size_t capacity)
{
    if (capacity >= 1UL << Object::miscBits)
        throw Error("attribute set of size %d is too big", capacity);
    return gc.alloc<Bindings>(Bindings::wordsFor(capacity), capacity);
}


void EvalState::mkAttrs(Value & v, size_t capacity)
{
    if (capacity == 0) {
        v.attrs = emptyBindings;
        v.type = tAttrs;
    } else {
        v.attrs = Bindings::allocBindings(capacity);
        v.type = tAttrs;
        nrAttrsets++;
        nrAttrsInAttrsets += capacity;
    }
}


/* Create a new attribute named 'name' on an existing attribute set stored
   in 'vAttrs' and return the newly allocated Value which is associated with
   this attribute. */
Value * EvalState::allocAttr(Value & vAttrs, const Symbol & name)
{
    auto v = allocValue();
    vAttrs.attrs->push_back(Attr(name, v));
    return v;
}


void Bindings::sort()
{
    std::sort(begin(), end());
}


}
