#include "search-path.hh"

namespace nix {

std::optional<std::string_view> SearchPath::Prefix::suffixIfPotentialMatch(
    std::string_view path) const
{
    auto n = s.size();

    /* Non-empty prefix and suffix must be separated by a /, or the
       prefix is not a valid path prefix. */
    bool needSeparator = n > 0 && n < path.size();

    if (needSeparator && path[n] != '/') {
        return std::nullopt;
    }

    /* Prefix must be prefix of this path. */
    if (path.compare(0, n, s) != 0) {
        return std::nullopt;
    }

    /* Skip next path separator. */
    return {
        path.substr(needSeparator ? n + 1 : n)
    };
}


SearchPath::Elem SearchPath::Elem::parse(std::string_view rawElem)
{
    size_t pos = rawElem.find('=');

    return SearchPath::Elem {
        .prefix = Prefix {
            .s = pos == std::string::npos
                ? std::string { "" }
                : std::string { rawElem.substr(0, pos) },
        },
        .path = Path {
            .s = std::string { rawElem.substr(pos + 1) },
        },
    };
}


SearchPath SearchPath::parse(const Strings & rawElems)
{
    SearchPath res;
    for (auto & rawElem : rawElems)
        res.elements.emplace_back(SearchPath::Elem::parse(rawElem));
    return res;
}

}
