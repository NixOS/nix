#include "nix/expr/search-path.hh"
#include "nix/util/split.hh"

namespace nix {

std::optional<std::string_view> LookupPath::Prefix::suffixIfPotentialMatch(std::string_view path) const
{
    auto n = s.size();

    /* Non-empty prefix and suffix must be separated by a /, or the
       prefix is not a valid path prefix. */
    bool needSeparator = n > 0 && n < path.size();

    if (needSeparator && path[n] != '/') {
        return std::nullopt;
    }

    /* Prefix must be prefix of this path. */
    if (!path.starts_with(s)) {
        return std::nullopt;
    }

    /* Skip next path separator. */
    return {path.substr(needSeparator ? n + 1 : n)};
}

LookupPath::Elem LookupPath::Elem::parse(std::string_view rawElem)
{
    auto split = splitOnce(rawElem, '=');

    return LookupPath::Elem{
        .prefix =
            Prefix{
                .s = split ? std::string{split->first} : std::string{""},
            },
        .path =
            Path{
                .s = split ? std::string{split->second} : std::string{rawElem},
            },
    };
}

LookupPath LookupPath::parse(const Strings & rawElems)
{
    LookupPath res;
    for (auto & rawElem : rawElems)
        res.elements.emplace_back(LookupPath::Elem::parse(rawElem));
    return res;
}

} // namespace nix
