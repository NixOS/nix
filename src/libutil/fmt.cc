#include "fmt.hh"

#include <regex>

namespace nix {

std::string hiliteMatches(
    std::string_view s,
    std::vector<std::smatch> matches,
    std::string_view prefix,
    std::string_view postfix)
{
    // Avoid copy on zero matches
    if (matches.size() == 0)
        return (std::string) s;

    std::sort(matches.begin(), matches.end(), [](const auto & a, const auto & b) {
        return a.position() < b.position();
    });

    std::string out;
    ssize_t last_end = 0;

    for (auto it = matches.begin(); it != matches.end();) {
        auto m = *it;
        size_t start = m.position();
        out.append(s.substr(last_end, m.position() - last_end));
        // Merge continous matches
        ssize_t end = start + m.length();
        while (++it != matches.end() && (*it).position() <= end) {
            auto n = *it;
            ssize_t nend = start + (n.position() - start + n.length());
            if (nend > end)
                end = nend;
        }
        out.append(prefix);
        out.append(s.substr(start, end - start));
        out.append(postfix);
        last_end = end;
    }

    out.append(s.substr(last_end));
    return out;
}

}
