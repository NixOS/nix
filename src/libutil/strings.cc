#include <filesystem>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>

#include "nix/util/strings-inline.hh"
#include "nix/util/os-string.hh"
#include "nix/util/error.hh"

#include <boost/lexical_cast.hpp>

namespace nix {

template std::list<std::string> tokenizeString(std::string_view s, std::string_view separators);
template StringSet tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);

template std::list<std::string> splitString(std::string_view s, std::string_view separators);
template StringSet splitString(std::string_view s, std::string_view separators);
template std::vector<std::string> splitString(std::string_view s, std::string_view separators);

template std::list<OsString>
basicSplitString(std::basic_string_view<OsChar> s, std::basic_string_view<OsChar> separators);

template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
template std::string concatStringsSep(std::string_view, const StringSet &);
template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);
template std::string concatStringsSep(std::string_view, const boost::container::small_vector<std::string, 64> &);

typedef std::string_view strings_2[2];
template std::string concatStringsSep(std::string_view, const strings_2 &);
typedef std::string_view strings_3[3];
template std::string concatStringsSep(std::string_view, const strings_3 &);
typedef std::string_view strings_4[4];
template std::string concatStringsSep(std::string_view, const strings_4 &);

template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::list<std::string> &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const StringSet &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::vector<std::string> &);

Strings quoteFSPaths(const std::set<std::filesystem::path> & paths, char quote)
{
    Strings res;
    for (auto & p : paths)
        res.push_back(quoteString(p.string(), quote));
    return res;
}

std::string chomp(std::string_view s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == s.npos ? "" : std::string(s, 0, i + 1);
}

std::string trim(std::string_view s, std::string_view whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == s.npos)
        return "";
    auto j = s.find_last_not_of(whitespace);
    return std::string(s, i, j == s.npos ? j : j - i + 1);
}

std::string replaceStrings(std::string res, std::string_view from, std::string_view to)
{
    if (from.empty())
        return res;
    size_t pos = 0;
    while ((pos = res.find(from, pos)) != res.npos) {
        res.replace(pos, from.size(), to);
        pos += to.size();
    }
    return res;
}

std::string rewriteStrings(std::string s, const StringMap & rewrites)
{
    for (auto & i : rewrites) {
        if (i.first == i.second)
            continue;
        size_t j = 0;
        while ((j = s.find(i.first, j)) != s.npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}

template<class N>
std::optional<N> string2Int(const std::string_view s)
{
    if (s.substr(0, 1) == "-" && !std::numeric_limits<N>::is_signed)
        return std::nullopt;
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}

template std::optional<unsigned char> string2Int<unsigned char>(const std::string_view s);
template std::optional<unsigned short> string2Int<unsigned short>(const std::string_view s);
template std::optional<unsigned int> string2Int<unsigned int>(const std::string_view s);
template std::optional<unsigned long> string2Int<unsigned long>(const std::string_view s);
template std::optional<unsigned long long> string2Int<unsigned long long>(const std::string_view s);
template std::optional<signed char> string2Int<signed char>(const std::string_view s);
template std::optional<signed short> string2Int<signed short>(const std::string_view s);
template std::optional<signed int> string2Int<signed int>(const std::string_view s);
template std::optional<signed long> string2Int<signed long>(const std::string_view s);
template std::optional<signed long long> string2Int<signed long long>(const std::string_view s);

template<class N>
std::optional<N> string2Float(const std::string_view s)
{
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}

template std::optional<double> string2Float<double>(const std::string_view s);
template std::optional<float> string2Float<float>(const std::string_view s);

bool hasPrefix(std::string_view s, std::string_view prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool hasSuffix(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string toLower(std::string s)
{
    for (auto & c : s)
        c = std::tolower(c);
    return s;
}

std::string escapeShellArgAlways(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += '\'';
    for (auto & i : s)
        if (i == '\'')
            r += "'\\''";
        else
            r += i;
    r += '\'';
    return r;
}

std::string stripIndentation(std::string_view s)
{
    size_t minIndent = 10000;
    size_t curIndent = 0;
    bool atStartOfLine = true;

    for (auto & c : s) {
        if (atStartOfLine && c == ' ')
            curIndent++;
        else if (c == '\n') {
            if (atStartOfLine)
                minIndent = std::max(minIndent, curIndent);
            curIndent = 0;
            atStartOfLine = true;
        } else {
            if (atStartOfLine) {
                minIndent = std::min(minIndent, curIndent);
                atStartOfLine = false;
            }
        }
    }

    std::string res;

    size_t pos = 0;
    while (pos < s.size()) {
        auto eol = s.find('\n', pos);
        if (eol == s.npos)
            eol = s.size();
        if (eol - pos > minIndent)
            res.append(s.substr(pos + minIndent, eol - pos - minIndent));
        res.push_back('\n');
        pos = eol + 1;
    }

    return res;
}

std::pair<std::string_view, std::string_view> getLine(std::string_view s)
{
    auto newline = s.find('\n');

    if (newline == s.npos) {
        return {s, ""};
    } else {
        auto line = s.substr(0, newline);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        return {line, s.substr(newline + 1)};
    }
}

/**
 * Shell split string: split a string into shell arguments, respecting quotes and backslashes.
 *
 * Used for NIX_SSHOPTS handling, which previously used `tokenizeString` and was broken by
 * Arguments that need to be passed to ssh with spaces in them.
 *
 * Read https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html for the
 * POSIX shell specification, which is technically what we are implementing here.
 */
std::list<std::string> shellSplitString(std::string_view s)
{
    std::list<std::string> result;
    std::string current;
    bool startedCurrent = false;
    bool escaping = false;

    auto pushCurrent = [&]() {
        if (startedCurrent) {
            result.push_back(current);
            current.clear();
            startedCurrent = false;
        }
    };

    auto pushChar = [&](char c) {
        current.push_back(c);
        startedCurrent = true;
    };

    auto pop = [&]() {
        auto c = s[0];
        s.remove_prefix(1);
        return c;
    };

    auto inDoubleQuotes = [&]() {
        startedCurrent = true;
        // in double quotes, escaping with backslash is only effective for $, `, ", and backslash
        while (!s.empty()) {
            auto c = pop();
            if (escaping) {
                switch (c) {
                case '$':
                case '`':
                case '"':
                case '\\':
                    pushChar(c);
                    break;
                default:
                    pushChar('\\');
                    pushChar(c);
                    break;
                }
                escaping = false;
            } else if (c == '\\') {
                escaping = true;
            } else if (c == '"') {
                return;
            } else {
                pushChar(c);
            }
        }
        if (s.empty()) {
            throw Error("unterminated double quote");
        }
    };

    auto inSingleQuotes = [&]() {
        startedCurrent = true;
        while (!s.empty()) {
            auto c = pop();
            if (c == '\'') {
                return;
            }
            pushChar(c);
        }
        if (s.empty()) {
            throw Error("unterminated single quote");
        }
    };

    while (!s.empty()) {
        auto c = pop();
        if (escaping) {
            pushChar(c);
            escaping = false;
        } else if (c == '\\') {
            escaping = true;
        } else if (c == ' ' || c == '\t') {
            pushCurrent();
        } else if (c == '"') {
            inDoubleQuotes();
        } else if (c == '\'') {
            inSingleQuotes();
        } else {
            pushChar(c);
        }
    }

    pushCurrent();

    return result;
}

std::string optionalBracket(std::string_view prefix, std::string_view content, std::string_view suffix)
{
    if (content.empty()) {
        return "";
    }
    std::string result;
    result.reserve(prefix.size() + content.size() + suffix.size());
    result.append(prefix);
    result.append(content);
    result.append(suffix);
    return result;
}

const char * requireCString(const std::string & s)
{
    if (std::memchr(s.data(), '\0', s.size())) [[unlikely]] {
        using namespace std::string_view_literals;
        auto str = replaceStrings(s, "\0"sv, "␀"sv);
        throw Error("string '%s' with null (\\0) bytes used where it's not allowed", str);
    }
    return s.c_str();
}

} // namespace nix
