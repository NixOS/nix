#include "symbol-table.hh"
#include "logging.hh"

#include <sys/mman.h>

namespace nix {

static void * allocateLazyMemory(size_t maxSize)
{
    auto p = mmap(nullptr, maxSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        throw SysError("allocating arena using mmap");
    return p;
}

ContiguousArena::ContiguousArena(size_t maxSize)
    : data((char *) allocateLazyMemory(maxSize))
    , maxSize(maxSize)
{
}

size_t ContiguousArena::allocate(size_t bytes)
{
    auto offset = size.fetch_add(bytes);
    if (offset + bytes > maxSize)
        throw Error("arena ran out of space");
    return offset;
}

Symbol SymbolTable::create(std::string_view s)
{
    {
        auto state(state_.read());
        auto it = state->symbols.find(s);
        if (it != state->symbols.end()) return Symbol(it->second);
    }

    // Most symbols are looked up more than once, so we trade off insertion performance
    // for lookup performance.
    // TODO: could probably be done more efficiently with transparent Hash and Equals
    // on the original implementation using unordered_set
    auto state(state_.lock());
    auto it = state->symbols.find(s);
    if (it != state->symbols.end()) return Symbol(it->second);

    // Atomically allocate space for the symbol in the arena.
    auto id = arena.allocate(s.size() + 1);
    auto p = const_cast<char *>(arena.data) + id;
    memcpy(p, s.data(), s.size());
    p[s.size()] = 0;

    state->symbols.emplace(std::string_view(p, s.size()), id);

    return Symbol(id);
}

}
