#include "nix/util/util.hh"
#include "nix/util/fmt.hh"
#include "nix/util/file-path.hh"
#include "nix/util/signals.hh"

#include <array>
#include <cctype>
#include <iostream>
#include <regex>

#include <sodium.h>
#include <boost/lexical_cast.hpp>
#include <stdint.h>

#ifdef NDEBUG
#  error "Nix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {

void initLibUtil()
{
    // Check that exception handling works. Exception handling has been observed
    // not to work on darwin when the linker flags aren't quite right.
    // In this case we don't want to expose the user to some unrelated uncaught
    // exception, but rather tell them exactly that exception handling is
    // broken.
    // When exception handling fails, the message tends to be printed by the
    // C++ runtime, followed by an abort.
    // For example on macOS we might see an error such as
    // libc++abi: terminating with uncaught exception of type nix::SystemError: error: C++ exception handling is broken.
    // This would appear to be a problem with the way Nix was compiled and/or linked and/or loaded.
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
    for (auto & s : ss)
        res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}

//////////////////////////////////////////////////////////////////////

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

// Explicitly instantiated in one place for faster compilation
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

static const int64_t conversionNumber = 1024;

SizeUnit getSizeUnit(int64_t value)
{
    auto unit = sizeUnits.begin();
    uint64_t absValue = std::abs(value);
    while (absValue > conversionNumber && unit < sizeUnits.end()) {
        unit++;
        absValue /= conversionNumber;
    }
    return *unit;
}

std::optional<SizeUnit> getCommonSizeUnit(std::initializer_list<int64_t> values)
{
    assert(values.size() > 0);

    auto it = values.begin();
    SizeUnit unit = getSizeUnit(*it);
    it++;

    for (; it != values.end(); it++) {
        if (unit != getSizeUnit(*it)) {
            return std::nullopt;
        }
    }

    return unit;
}

std::string renderSizeWithoutUnit(int64_t value, SizeUnit unit, bool align)
{
    // bytes should also displayed as KiB => 100 Bytes => 0.1 KiB
    auto power = std::max<std::underlying_type_t<SizeUnit>>(1, std::to_underlying(unit));
    double denominator = std::pow(conversionNumber, power);
    double result = (double) value / denominator;
    return fmt(align ? "%6.1f" : "%.1f", result);
}

char getSizeUnitSuffix(SizeUnit unit)
{
    switch (unit) {
#define NIX_UTIL_DEFINE_SIZE_UNIT(name, suffix) \
    case SizeUnit::name:                        \
        return suffix;
        NIX_UTIL_SIZE_UNITS
#undef NIX_UTIL_DEFINE_SIZE_UNIT
    }

    assert(false);
}

std::string renderSize(int64_t value, bool align)
{
    SizeUnit unit = getSizeUnit(value);
    return fmt("%s %ciB", renderSizeWithoutUnit(value, unit, align), getSizeUnitSuffix(unit));
}

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

void ignoreExceptionInDestructor(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (Error & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.info().msg);
        } catch (std::exception & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.what());
        }
    } catch (...) {
    }
}

void ignoreExceptionExceptInterrupt(Verbosity lvl)
{
    try {
        throw;
    } catch (const Interrupted & e) {
        throw;
    } catch (Error & e) {
        printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.info().msg);
    } catch (std::exception & e) {
        printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.what());
    }
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

} // namespace nix
