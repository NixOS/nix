#include <filesystem>
#include <string>
#include <sstream>

#include "strings-inline.hh"
#include "os-string.hh"

namespace nix {

struct view_stringbuf : public std::stringbuf
{
    inline std::string_view toView()
    {
        auto begin = pbase();
        return {begin, begin + pubseekoff(0, std::ios_base::cur, std::ios_base::out)};
    }
};

std::string_view toView(const std::ostringstream & os)
{
    auto buf = static_cast<view_stringbuf *>(os.rdbuf());
    return buf->toView();
}

template std::list<std::string> tokenizeString(std::string_view s, std::string_view separators);
template std::set<std::string> tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);

template std::list<std::string> splitString(std::string_view s, std::string_view separators);
template std::set<std::string> splitString(std::string_view s, std::string_view separators);
template std::vector<std::string> splitString(std::string_view s, std::string_view separators);

template std::list<OsString>
basicSplitString(std::basic_string_view<OsChar> s, std::basic_string_view<OsChar> separators);

template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
template std::string concatStringsSep(std::string_view, const std::set<std::string> &);
template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);

typedef std::string_view strings_2[2];
template std::string concatStringsSep(std::string_view, const strings_2 &);
typedef std::string_view strings_3[3];
template std::string concatStringsSep(std::string_view, const strings_3 &);
typedef std::string_view strings_4[4];
template std::string concatStringsSep(std::string_view, const strings_4 &);

template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::list<std::string> &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::set<std::string> &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::vector<std::string> &);

/**
 * Shell split string: split a string into shell arguments, respecting quotes and backslashes.
 *
 * Used for NIX_SSHOPTS handling, which previously used `tokenizeString` and was broken by
 * Arguments that need to be passed to ssh with spaces in them.
 */
std::list<std::string> shellSplitString(std::string_view s)
{
    std::list<std::string> result;
    std::string current;
    bool inQuoteSingle = false;
    bool inQuoteDouble = false;
    bool escaped = false;
    bool lastCharWasQuote = false;
    for (char c : s) {
        lastCharWasQuote = false;
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '\'') {
            if (inQuoteSingle) {
                inQuoteSingle = false;
                lastCharWasQuote = true;
            } else if (!inQuoteDouble) {
                inQuoteSingle = true;
                lastCharWasQuote = true;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            if (inQuoteDouble) {
                inQuoteDouble = false;
                lastCharWasQuote = true;
            } else if (!inQuoteSingle) {
                inQuoteDouble = true;
                lastCharWasQuote = true;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inQuoteSingle || inQuoteDouble) {
                current.push_back(c);
            } else {
                result.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty() || lastCharWasQuote) {
        result.push_back(current);
    }
    return result;
}
} // namespace nix
