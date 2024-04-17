#include "util.hh"
#include "fmt.hh"

#include <array>
#include <cctype>
#include <iostream>
#include <regex>

#include <sodium.h>

#ifdef NDEBUG
#error "Nix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {

void initLibUtil() {
    // Check that exception handling works. Exception handling has been observed
    // not to work on darwin when the linker flags aren't quite right.
    // In this case we don't want to expose the user to some unrelated uncaught
    // exception, but rather tell them exactly that exception handling is
    // broken.
    // When exception handling fails, the message tends to be printed by the
    // C++ runtime, followed by an abort.
    // For example on macOS we might see an error such as
    // libc++abi: terminating with uncaught exception of type nix::SystemError: error: C++ exception handling is broken. This would appear to be a problem with the way Nix was compiled and/or linked and/or loaded.
    bool caught = false;
    try {
        throwExceptionSelfCheck();
    } catch (const nix::Error & _e) {
        caught = true;
    }
    // This is not actually the main point of this check, but let's make sure anyway:
    assert(caught);

    if (sodium_init() == -1)
        throw Error("could not initialise libsodium");
}

//////////////////////////////////////////////////////////////////////

std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    for (auto & s : ss) res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}


//////////////////////////////////////////////////////////////////////


template<class C> C tokenizeString(std::string_view s, std::string_view separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != s.npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == s.npos) end = s.size();
        result.insert(result.end(), std::string(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template Strings tokenizeString(std::string_view s, std::string_view separators);
template StringSet tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);


std::string chomp(std::string_view s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == s.npos ? "" : std::string(s, 0, i + 1);
}


std::string trim(std::string_view s, std::string_view whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == s.npos) return "";
    auto j = s.find_last_not_of(whitespace);
    return std::string(s, i, j == s.npos ? j : j - i + 1);
}


std::string replaceStrings(
    std::string res,
    std::string_view from,
    std::string_view to)
{
    if (from.empty()) return res;
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
        if (i.first == i.second) continue;
        size_t j = 0;
        while ((j = s.find(i.first, j)) != s.npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}


bool hasPrefix(std::string_view s, std::string_view prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}


bool hasSuffix(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size()
        && s.substr(s.size() - suffix.size()) == suffix;
}


std::string toLower(std::string s)
{
    for (auto & c : s)
        c = std::tolower(c);
    return s;
}


std::string shellEscape(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += '\'';
    for (auto & i : s)
        if (i == '\'') r += "'\\''"; else r += i;
    r += '\'';
    return r;
}


void ignoreException(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (std::exception & e) {
            printMsg(lvl, "error (ignored): %1%", e.what());
        }
    } catch (...) { }
}


constexpr char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view s)
{
    std::string res;
    res.reserve((s.size() + 2) / 3 * 4);
    int data = 0, nbits = 0;

    for (char c : s) {
        data = data << 8 | (unsigned char) c;
        nbits += 8;
        while (nbits >= 6) {
            nbits -= 6;
            res.push_back(base64Chars[data >> nbits & 0x3f]);
        }
    }

    if (nbits) res.push_back(base64Chars[data << (6 - nbits) & 0x3f]);
    while (res.size() % 4) res.push_back('=');

    return res;
}


std::string base64Decode(std::string_view s)
{
    constexpr char npos = -1;
    constexpr std::array<char, 256> base64DecodeChars = [&] {
        std::array<char, 256>  result{};
        for (auto& c : result)
            c = npos;
        for (int i = 0; i < 64; i++)
            result[base64Chars[i]] = i;
        return result;
    }();

    std::string res;
    // Some sequences are missing the padding consisting of up to two '='.
    //                    vvv
    res.reserve((s.size() + 2) / 4 * 3);
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=') break;
        if (c == '\n') continue;

        char digit = base64DecodeChars[(unsigned char) c];
        if (digit == npos)
            throw Error("invalid character in Base64 string: '%c'", c);

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
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
        if (eol == s.npos) eol = s.size();
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


std::string showBytes(uint64_t bytes)
{
    return fmt("%.2f MiB", bytes / (1024.0 * 1024.0));
}

}
