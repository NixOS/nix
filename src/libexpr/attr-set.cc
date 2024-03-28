#include "attr-set.hh"
#include "eval-inline.hh"

#include <algorithm>


namespace nix {



/* Allocate a new array of attributes for an attribute set with a specific
   capacity. The space is implicitly reserved after the Bindings
   structure. */
Bindings * EvalState::allocBindings(size_t capacity)
{
    if (capacity == 0)
        return &emptyBindings;
    if (capacity > std::numeric_limits<Bindings::size_t>::max())
        throw Error("attribute set of size %d is too big", capacity);
    nrAttrsets++;
    nrAttrsInAttrsets += capacity;
    return new (allocBytes(sizeof(Bindings) + sizeof(Attr) * capacity)) Bindings((Bindings::size_t) capacity);
}


Value & BindingsBuilder::alloc(Symbol name, PosIdx pos)
{
    auto value = state.allocValue();
    bindings->push_back(Attr(name, value, pos));
    return *value;
}


Value & BindingsBuilder::alloc(std::string_view name, PosIdx pos)
{
    return alloc(state.symbols.create(name), pos);
}


void Bindings::sort()
{
    if (size_) std::sort(begin(), end());
}


Value & Value::mkAttrs(BindingsBuilder & bindings)
{
    mkAttrs(bindings.finish());
    return *this;
}


}
