#include "nix/expr/symbol-table.hh"
#include "nix/util/logging.hh"

#include <sys/mman.h>

namespace nix {

#ifndef MAP_NORESERVE
#  define MAP_NORESERVE 0
#endif

static void * allocateLazyMemory(size_t maxSize)
{
    auto p = mmap(nullptr, maxSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
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
    uint32_t idx;

    auto visit = [&](const SymbolStr & sym) { idx = ((const char *) sym.s) - arena.data; };

    symbols.insert_and_visit(SymbolStr::Key{s, arena}, visit, visit);

    return Symbol(idx);
}

SymbolStr::SymbolStr(const SymbolStr::Key & key)
{
    auto rawSize = sizeof(Value) + key.s.size() + 1;
    auto size = ((rawSize + SymbolTable::alignment - 1) / SymbolTable::alignment) * SymbolTable::alignment;

    auto id = key.arena.allocate(size);

    auto v = (SymbolValue *) (const_cast<char *>(key.arena.data) + id);
    auto s = (char *) (v + 1);

    memcpy(s, key.s.data(), key.s.size());
    s[key.s.size()] = 0;

    v->mkString(s, nullptr);

    this->s = v;
}

}
