#include "nix/store/derivation/v2/derivation.hh"
#include "nix/util/overloaded.hh"

namespace nix::derivation::v2 {

void Interpolated::append(std::string literal)
{
    if (literal.empty())
        return;
    if (!pieces_.empty())
        if (auto * last = std::get_if<std::string>(&pieces_.back())) {
            *last += literal;
            return;
        }
    pieces_.emplace_back(std::move(literal));
}

void Interpolated::append(Reference reference)
{
    pieces_.emplace_back(std::move(reference));
}

std::string Interpolated::render(fun<std::string(const Reference &)> resolve) const
{
    std::string res;
    for (auto & piece : pieces_)
        std::visit(
            overloaded{
                [&](const std::string & literal) { res += literal; },
                [&](const Reference & ref) { res += resolve(ref); },
            },
            piece);
    return res;
}

} // namespace nix::derivation::v2
