#include "value-path.hh"
#include "nix/expr/print.hh"
#include "nix/util/util.hh"

#include <sstream>

namespace nix {

std::string showValuePath(const SymbolTable & symbols, const ValuePath & p)
{
    if (p.empty())
        return "the top-level value";
    std::ostringstream out;
    out << "the value at ";
    for (auto & seg : p)
        std::visit(
            overloaded{
                [&](Symbol s) {
                    out << '.';
                    printAttributeName(out, symbols[s]);
                },
                [&](size_t i) { out << '[' << i << ']'; },
            },
            seg);
    return out.str();
}

} // namespace nix
