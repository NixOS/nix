#include "attr-set.hh"
#include "eval.hh"

#include <algorithm>


namespace nix {


static void * allocBytes(size_t n)
{
    void * p;
#if HAVE_BOEHMGC
    p = GC_malloc(n);
#else
    p = malloc(n);
#endif
    if (!p) throw std::bad_alloc();
    return p;
}


/* Allocate a new array of attributes for an attribute set with a specific
   capacity. The space is implicitly reserved after the Bindings
   structure. */
Bindings * EvalState::allocBindings(Bindings::size_t capacity)
{
    return new (allocBytes(sizeof(Bindings) + sizeof(Attr) * capacity)) Bindings(capacity);
}


void EvalState::mkAttrs(Value & v, unsigned int capacity)
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
