#include <filesystem>
#include <string>
#include <sstream>

#include "strings-inline.hh"
#include "os-string.hh"
#include "error.hh"

namespace nix {

struct view_stringbuf : public std::stringbuf
{
    inline std::string_view toView()
    {
        auto begin = pbase();
        return {begin, begin + pubseekoff(0, std::ios_base::cur, std::ios_base::out)};
    }
};

__attribute__((no_sanitize("undefined"))) std::string_view toView(const std::ostringstream & os)
{
    /* Downcasting like this is very much undefined behavior, so we disable
       UBSAN for this function. */
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
template std::string concatStringsSep(std::string_view, const boost::container::small_vector<std::string, 64> &);

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
} // namespace nix
