#pragma once
///@file
///
/// Private to libstore: the generic building blocks of the "ATerm"
/// derivation formats, shared by the version 1 and version 2 parsers
/// and printers. They correspond to the common productions of the
/// grammar in `doc/manual/source/protocols/derivation-aterm.md`.

#include "nix/store/store-dir-config.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/util/strings-inline.hh"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace nix::derivation::aterm {

/**
 * This mimics std::istream to some extent. We use this much smaller implementation
 * instead of plain istreams because the sentry object overhead is too high.
 */
struct StringViewStream
{
    std::string_view remaining;

    int peek() const
    {
        if (remaining.empty())
            return EOF;
        return static_cast<unsigned char>(remaining[0]);
    }

    int get()
    {
        auto c = peek();
        if (c != EOF)
            remaining.remove_prefix(1);
        return c;
    }
};

/**
 * Maps a character to its counterpart in an escape sequence, with
 * `'\0'` meaning "no such escape". That is a usable sentinel because it
 * is neither an escape character nor the meaning of one.
 */
struct EscapeMap
{
    using Map = std::array<char, 256>;

    constexpr EscapeMap(Map map)
        : map(map)
    {
    }

    constexpr std::optional<char> lookup(char c) const
    {
        auto res = map[(unsigned char) c];
        return res ? std::optional{res} : std::nullopt;
    }

private:
    Map map;
};

/* The character an escape sequence stands for, e.g. `'n'` -> `'\n'`. */
inline constexpr EscapeMap escapes = [] {
    EscapeMap::Map map{};
    map[(unsigned char) 'n'] = '\n';
    map[(unsigned char) 'r'] = '\r';
    map[(unsigned char) 't'] = '\t';
    map[(unsigned char) '\\'] = '\\';
    map[(unsigned char) '\"'] = '\"';
    return EscapeMap{map};
}();

/* The inverse of `escapes`: the character which must be escaped to
   write the given one, e.g. `'\n'` -> `'n'`. */
inline constexpr EscapeMap unescapes = [] {
    EscapeMap::Map map{};
    for (size_t i = 0; i < map.size(); i++)
        if (auto escaped = escapes.lookup((char) i))
            map[(unsigned char) *escaped] = char(i);
    return EscapeMap{map};
}();


/* Read string `s' from stream `str'. */
inline void expect(StringViewStream & str, std::string_view s)
{
    if (!str.remaining.starts_with(s))
        throw FormatError("expected string '%1%'", s);
    str.remaining.remove_prefix(s.size());
}

inline void expect(StringViewStream & str, char c)
{
    if (str.remaining.empty() || str.remaining[0] != c)
        throw FormatError("expected '%1%' character", c);
    str.remaining.remove_prefix(1);
}

/**
 * Read a C-style string from stream `str'.
 *
 * This is for the fields whose contents are arbitrary, and so may need
 * escaping: the environment variables, the builder, and its arguments.
 */
inline BackedStringView parseString(StringViewStream & str)
{
    expect(str, '"');

    const auto next = [&]() -> char {
        auto ch = str.get();
        if (ch == EOF) {
            throw FormatError("unterminated string in derivation");
        }
        return (char) ch;
    };

    std::string res;

    while (true) {
        auto ch = next();
        if (ch == '"') {
            return res;
        } else if ((char) ch == '\\') {
            ch = next();
            auto escaped = escapes.lookup(ch);
            if (!escaped.has_value()) {
                throw FormatError("unknown escape '\\%1%'", ch);
            }
            res.push_back(escaped.value());
        } else if (unescapes.lookup(ch).has_value()) {
            // Characters that can be escaped must be escaped
            throw FormatError("invalid character in string");
        } else {
            res.push_back(ch);
        }
    }
}

/**
 * Read a string which must not contain any escape sequence, and so can
 * be taken verbatim from the input.
 *
 * This is for the fields drawn from restricted alphabets: store paths,
 * output names, and the platform. An escape in one of those is not
 * merely redundant but a second encoding of the same value, so it is
 * rejected.
 */
inline BackedStringView parseUnquotedString(StringViewStream & str)
{
    expect(str, '"');
    auto end = str.remaining.find('"');
    if (end == std::string_view::npos)
        throw FormatError("unterminated string in derivation");
    auto content = str.remaining.substr(0, end);
    // Already know that it ends in an endquote from the find, no need to check again
    str.remaining.remove_prefix(end + 1);
    if (content.find('\\') != std::string_view::npos)
        throw FormatError("unexpected escape sequence in unquoted string");

    return content;
}

/**
 * Read the string of a store path.
 *
 * Unless the store directory needs escaping, it is written verbatim,
 * and an escape in it is rejected. See
 * `defaultSupportWindowsStoreDir`.
 *
 * Separate from `parseStorePath` because an output's path field shares
 * the encoding but may also be empty, which is no store path at all.
 */
inline BackedStringView parseStorePathString(StringViewStream & str, bool supportWindowsStoreDir)
{
    return supportWindowsStoreDir ? parseString(str) : parseUnquotedString(str);
}

/* Store paths in derivations must be written in their canonical form. */
inline StorePath parseStorePath(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    return store.parseStorePathCanonical(*parseStorePathString(str, supportWindowsStoreDir));
}

/**
 * Parse a `[...]`-delimited list, calling `parseItem` for each item.
 */
inline void parseList(StringViewStream & str, auto parseItem)
{
    expect(str, '[');

    /* The empty list is the one case with no item to parse. */
    if (str.peek() == ']') {
        str.get();
        return;
    }

    while (true) {
        parseItem();
        auto ch = str.get();
        if (ch == ']')
            return;
        if (ch != ',')
            throw FormatError("invalid list");
    }
}

/**
 * The key of a container element: the element itself for a set, and the
 * first half of the pair for a map.
 */
template<typename T>
inline const T & keyOf(const T & elem)
{
    return elem;
}

template<typename K, typename V>
inline const K & keyOf(const std::pair<const K, V> & elem)
{
    return elem.first;
}

/**
 * Check that `key` may be appended to an ordered container: the ATerm
 * encoding is canonical, so items must appear in ascending order, and
 * not be duplicated, lest the same value have multiple encodings.
 *
 * Comparing against the last element rather than inspecting an insert
 * result means the caller can then insert with a hint of `end()`, which
 * is amortized constant time rather than logarithmic.
 *
 * @param describe renders the offending item for the error message. It
 * is a callback because rendering can be expensive, and we only need it
 * when something is wrong.
 */
inline void checkMonotonic(const auto & c, const auto & key, auto describe)
{
    if (c.empty())
        return;
    const auto & last = keyOf(*c.rbegin());
    if (last == key) [[unlikely]] /* Must not be duplicated. */
        throw FormatError("duplicate %s", describe());
    if (!(last < key)) [[unlikely]]
        throw FormatError("%s does not appear in a sorted order", describe());
}

/**
 * Parse a list of `(key, value)` pairs into an ordered map, requiring
 * the keys to be monotonic per `checkMonotonic`.
 *
 * @param parseKey parses a key
 * @param parseValue parses the value belonging to the just-parsed key
 * @param describe renders a key for error messages
 */
inline void parseMap(StringViewStream & str, auto & map, auto parseKey, auto parseValue, auto describe)
{
    parseList(str, [&] {
        expect(str, '(');
        auto key = parseKey();
        expect(str, ',');
        auto value = parseValue(key);
        checkMonotonic(map, key, [&] { return describe(key); });
        map.emplace_hint(map.end(), std::move(key), std::move(value));
        expect(str, ')');
    });
}

inline StringSet parseStrings(StringViewStream & str)
{
    StringSet res;
    parseList(str, [&] {
        auto content = parseUnquotedString(str).toOwned();
        checkMonotonic(res, content, [&] { return fmt("set item '%s'", content); });
        res.insert(res.end(), std::move(content));
    });
    return res;
}

inline StorePathSet parseStorePaths(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    StorePathSet res;
    parseList(str, [&] {
        auto path = parseStorePath(store, str, supportWindowsStoreDir);
        checkMonotonic(res, path, [&] { return fmt("store path '%s'", store.printStorePath(path)); });
        res.insert(res.end(), std::move(path));
    });
    return res;
}

/**
 * Print a derivation string literal to an `std::string`.
 *
 * This syntax does not generalize to the expression language, which needs to
 * escape `$`.
 *
 * @param res Where to print to
 * @param s Which logical string to print
 */
inline void printString(std::string & res, std::string_view s)
{
    res += '"';
    static constexpr auto chunkSize = 1024;
    std::array<char, 2 * chunkSize + 2> buffer;
    while (!s.empty()) {
        auto chunk = s.substr(0, /*n=*/chunkSize);
        s.remove_prefix(chunk.size());
        char * buf = buffer.data();
        char * p = buf;
        for (auto c : chunk) {
            auto escape = unescapes.lookup(c);
            if (escape.has_value()) {
                *p++ = '\\';
                *p++ = escape.value();
            } else {
                *p++ = c;
            }
        }
        res.append(buf, p - buf);
    }
    res += '"';
}

inline void printUnquotedString(std::string & res, std::string_view s)
{
    res += '"';
    res.append(s);
    res += '"';
}

/**
 * Print a `[...]`-delimited list, calling `printItem` for each item.
 */
inline void printList(std::string & res, const auto & items, auto printItem)
{
    res += '[';
    bool first = true;
    for (auto & item : items) {
        if (first)
            first = false;
        else
            res += ',';
        printItem(item);
    }
    res += ']';
}

/**
 * Print a map as a list of `(key,value)` pairs, calling `printKey` and
 * `printValue` for each entry. The map is already in the ascending order
 * `parseMap` requires.
 */
inline void printMap(std::string & res, const auto & map, auto printKey, auto printValue)
{
    printList(res, map, [&](const auto & entry) {
        res += '(';
        printKey(entry.first);
        res += ',';
        printValue(entry.first, entry.second);
        res += ')';
    });
}

inline void printStrings(std::string & res, const auto & strings)
{
    printList(res, strings, [&](const auto & s) { printString(res, s); });
}

inline void printUnquotedStrings(std::string & res, const auto & strings)
{
    printList(res, strings, [&](const auto & s) { printUnquotedString(res, s); });
}

/* The counterpart of `parseStorePathString`. */
inline void printStorePathString(std::string & res, std::string_view pathS, bool supportWindowsStoreDir)
{
    if (supportWindowsStoreDir)
        printString(res, pathS);
    else
        printUnquotedString(res, pathS);
}

} // namespace nix::derivation::aterm
