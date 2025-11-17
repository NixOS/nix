#pragma once

#include "nix/util/strings.hh"

namespace nix {

template<class C, class CharT>
C basicTokenizeString(std::basic_string_view<CharT> s, std::basic_string_view<CharT> separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != s.npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == s.npos)
            end = s.size();
        result.insert(result.end(), std::basic_string<CharT>(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template<class C>
C tokenizeString(std::string_view s, std::string_view separators)
{
    return basicTokenizeString<C, char>(s, separators);
}

template<class C, class CharT>
void basicSplitStringInto(C & accum, std::basic_string_view<CharT> s, std::basic_string_view<CharT> separators)
{
    size_t pos = 0;
    while (pos <= s.size()) {
        auto end = s.find_first_of(separators, pos);
        if (end == s.npos)
            end = s.size();
        accum.insert(accum.end(), typename C::value_type{s.substr(pos, end - pos)});
        pos = end + 1;
    }
}

template<typename C>
void splitStringInto(C & accum, std::string_view s, std::string_view separators)
{
    basicSplitStringInto<C, char>(accum, s, separators);
}

template<class C, class CharT>
C basicSplitString(std::basic_string_view<CharT> s, std::basic_string_view<CharT> separators)
{
    C result;
    basicSplitStringInto(result, s, separators);
    return result;
}

template<class C>
C splitString(std::string_view s, std::string_view separators)
{
    return basicSplitString<C, char>(s, separators);
}

template<class CharT, class C>
std::basic_string<CharT> basicConcatStringsSep(const std::basic_string_view<CharT> sep, const C & ss)
{
    size_t size = 0;
    bool tail = false;
    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss) {
        if (tail)
            size += sep.size();
        size += std::basic_string_view<CharT>{s}.size();
        tail = true;
    }
    std::basic_string<CharT> s;
    s.reserve(size);
    tail = false;
    for (auto & i : ss) {
        if (tail)
            s += sep;
        s += i;
        tail = true;
    }
    return s;
}

template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss)
{
    return basicConcatStringsSep<char, C>(sep, ss);
}

template<class C>
std::string dropEmptyInitThenConcatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;

    // TODO? remove to make sure we don't rely on the empty item ignoring behavior,
    //       or just get rid of this function by understanding the remaining calls.
    // for (auto & i : ss) {
    //     // Make sure we don't rely on the empty item ignoring behavior
    //     assert(!i.empty());
    //     break;
    // }

    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss)
        size += sep.size() + std::string_view(s).size();
    std::string s;
    s.reserve(size);
    for (auto & i : ss) {
        if (s.size() != 0)
            s += sep;
        s += i;
    }
    return s;
}

/**
 * Consteval helper for the "constexpr 2-step" pattern.
 * Converts a std::string to a std::array<char, N> for stable compile-time storage.
 *
 * Usage:
 *   static constexpr auto str = stripIndentationConsteval(...);
 *   static constexpr auto arr = toArray<str.size()>(str);
 *   static constexpr std::string_view view{arr.data(), arr.size()};
 */
template<size_t N>
consteval std::string_view toArray(const std::string & s)
{
    static std::array<char, N> arr{};
    for (size_t i = 0; i < N; ++i) {
        arr[i] = s[i];
    }
    return {arr.data(), N};
}

constexpr std::string stripIndentationImpl(std::string_view s)
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

template<std::string_view s>
consteval std::string_view stripIndentationConsteval()
{
    constexpr auto stripped = stripIndentationImpl(s);
    constexpr size_t n = stripped.size();
    return toArray<n>(stripped);
}

} // namespace nix
