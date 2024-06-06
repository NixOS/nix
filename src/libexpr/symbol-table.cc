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
    std::size_t hash = std::hash<std::string_view>{}(s);
    auto domain = hash % symbolDomains.size();

    {
        auto symbols(symbolDomains[domain].read());
        auto it = symbols->find(s);
        if (it != symbols->end()) return Symbol(it->second);
    }

    // Most symbols are looked up more than once, so we trade off insertion performance
    // for lookup performance.
    // TODO: could probably be done more efficiently with transparent Hash and Equals
    // on the original implementation using unordered_set
    auto symbols(symbolDomains[domain].lock());
    auto it = symbols->find(s);
    if (it != symbols->end()) return Symbol(it->second);

    // Atomically allocate space for the symbol in the arena.
    auto id = arena.allocate(s.size() + 1);
    auto p = const_cast<char *>(arena.data) + id;
    memcpy(p, s.data(), s.size());
    p[s.size()] = 0;

    symbols->emplace(std::string_view(p, s.size()), id);

    return Symbol(id);
}

size_t SymbolTable::size() const
{
    size_t res = 0;
    for (auto & domain : symbolDomains)
        res += domain.read()->size();
    return res;
}


}
