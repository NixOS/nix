#include "attr-set.hh"
#include "eval-inline.hh"

#include <algorithm>


namespace nix {


/* Allocate a new array of attributes for an attribute set with a specific
   capacity. The space is implicitly reserved after the Bindings
   structure. */
Bindings * EvalState::allocBindings(size_t capacity)
{
    if (capacity > std::numeric_limits<Bindings::size_t>::max())
        throw Error("attribute set of size %d is too big", capacity);
    return new (allocBytes(sizeof(Bindings) + sizeof(Attr) * capacity)) Bindings((Bindings::size_t) capacity);
}


void EvalState::mkAttrs(Value & v, size_t capacity)
{
    if (capacity == 0) {
        v = vEmptySet;
        return;
    }
    clearValue(v);
    v.type = tAttrs;
    v.attrs = allocBindings(capacity);
    nrAttrsets++;
    nrAttrsInAttrsets += capacity;
}


/* Create a new attribute named 'name' on an existing attribute set stored
   in 'vAttrs' and return the newly allocated Value which is associated with
   this attribute. */
Value * EvalState::allocAttr(Value & vAttrs, const Symbol & name)
{
    Value * v = allocValue();
    vAttrs.attrs->push_back(Attr(name, v));
    return v;
}


void Bindings::sort()
{
    std::sort(begin(), end());
}


}
