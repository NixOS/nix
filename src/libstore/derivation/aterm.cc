#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/masked.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/json-utils.hh"

#include <boost/container/small_vector.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <optional>
#include <ranges>

namespace nix {

namespace derivation {

/* --------------------------------------------------------------------------
   ATerm parsing
   -------------------------------------------------------------------------- */

namespace {
/**
 * This mimics std::istream to some extent. We use this much smaller implementation
 * instead of plain istreams because the sentry object overhead is too high.
 */
struct StringViewStream
{
    std::string_view remaining;

    int peek() const
    {
        return remaining.empty() ? EOF : remaining[0];
    }

    int get()
    {
        if (remaining.empty())
            return EOF;
        char c = remaining[0];
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
constexpr EscapeMap escapes = [] {
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
constexpr EscapeMap unescapes = [] {
    EscapeMap::Map map{};
    for (size_t i = 0; i < map.size(); i++)
        if (auto escaped = escapes.lookup((char) i))
            map[(unsigned char) *escaped] = char(i);
    return EscapeMap{map};
}();

} // namespace

/* Read string `s' from stream `str'. */
static void expect(StringViewStream & str, std::string_view s)
{
    if (!str.remaining.starts_with(s))
        throw FormatError("expected string '%1%'", s);
    str.remaining.remove_prefix(s.size());
}

static void expect(StringViewStream & str, char c)
{
    if (str.remaining.empty() || str.remaining[0] != c)
        throw FormatError("expected string '%1%'", c);
    str.remaining.remove_prefix(1);
}

/**
 * Read a C-style string from stream `str'.
 *
 * This is for the fields whose contents are arbitrary, and so may need
 * escaping: the environment variables, the builder, and its arguments.
 */
static BackedStringView parseString(StringViewStream & str)
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
static BackedStringView parseUnquotedString(StringViewStream & str)
{
    expect(str, '"');
    auto end = str.remaining.find('"');
    if (end == std::string_view::npos)
        throw FormatError("unterminated string in derivation");
    auto content = str.remaining.substr(0, end);
    // Already know that it ends in an endquote from the find, no need to check again
    str.remaining.remove_prefix(end + 1);
    if (content.find('\\') != std::string_view::npos)
        throw FormatError("unexected escape sequence in unquoted string");

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
static BackedStringView parseStorePathString(StringViewStream & str, bool supportWindowsStoreDir)
{
    return supportWindowsStoreDir ? parseString(str) : parseUnquotedString(str);
}

/* Store paths in derivations must be written in their canonical form. */
static StorePath parseStorePath(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    return store.parseStorePathCanonical(*parseStorePathString(str, supportWindowsStoreDir));
}

/**
 * Parse a `[...]`-delimited list, calling `parseItem` for each item.
 */
static void parseList(StringViewStream & str, auto parseItem)
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
static const T & keyOf(const T & elem)
{
    return elem;
}

template<typename K, typename V>
static const K & keyOf(const std::pair<const K, V> & elem)
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
static void checkMonotonic(const auto & c, const auto & key, auto describe)
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
static void parseMap(StringViewStream & str, auto & map, auto parseKey, auto parseValue, auto describe)
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

static StringSet parseStrings(StringViewStream & str)
{
    StringSet res;
    parseList(str, [&] {
        auto content = parseUnquotedString(str).toOwned();
        checkMonotonic(res, content, [&] { return fmt("set item '%s'", content); });
        res.insert(res.end(), std::move(content));
    });
    return res;
}

static StorePathSet parseStorePaths(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    StorePathSet res;
    parseList(str, [&] {
        auto path = parseStorePath(store, str, supportWindowsStoreDir);
        checkMonotonic(res, path, [&] { return fmt("store path '%s'", store.printStorePath(path)); });
        res.insert(res.end(), std::move(path));
    });
    return res;
}

/* Defined with the unparser below; the error message for a bad
   `inputDrvs` entry renders its key the same way the printer would. */
static std::string keyToString(const StoreDirConfig & store, const StorePath & key);
static std::string keyToString(const StoreDirConfig &, const Hash & key);

/**
 * The inverse of `keyToString`: how an `inputDrvs` key is written, for
 * each input type that has one.
 */
template<typename Key>
static Key parseKey(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir);

template<>
StorePath parseKey<StorePath>(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    auto drvPath = parseStorePath(store, str, supportWindowsStoreDir);
    drvPath.requireDerivation();
    return drvPath;
}

template<>
Hash parseKey<Hash>(const StoreDirConfig &, StringViewStream & str, bool)
{
    return Hash::parseNonSRIUnprefixed(*parseString(str), HashAlgorithm::SHA256);
}

/**
 * An `inputDrvs` entry with no outputs cannot be represented in the
 * flat inputs set, and would thus be silently dropped rather than
 * round-tripped. Nix itself never produces one.
 */
static bool nodeIsEmpty(const std::set<OutputName, std::less<>> & node)
{
    return node.empty();
}

/**
 * The method and algorithm the three content-addressing alternatives
 * all begin by parsing out of the `hashAlgo` field.
 */
static std::pair<ContentAddressMethod, HashAlgorithm>
parseCaMethodAlgo(std::string_view hashAlgoStr, const ExperimentalFeatureSettings & xpSettings)
{
    if (hashAlgoStr.empty())
        throw FormatError("content-addressing derivation output must specify a hash algorithm");
    ContentAddressMethod method = ContentAddressMethod::parsePrefix(hashAlgoStr);
    if (method == ContentAddressMethod::Raw::Text)
        xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output");
    return {std::move(method), parseHashAlgo(hashAlgoStr)};
}

/**
 * Parse the three output fields as the alternative the caller expects.
 *
 * There is one specialization per alternative, and the `Output` one
 * dispatches on the syntax into them --- mirroring `unparseOutput`,
 * which has one overload per alternative and a `std::visit` to pick
 * between them.
 */
template<typename Out>
static Out parseOutput(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings);

template<>
Output::Deferred parseOutput<Output::Deferred>(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings &)
{
    if (!hashAlgoStr.empty())
        throw FormatError("deferred derivation output should not specify a hash algorithm");
    if (!hashS.empty())
        throw FormatError("deferred derivation output should not specify a hash");
    if (!pathS.empty())
        throw FormatError("deferred derivation output should not specify an output path");
    return {};
}

template<>
Output::InputAddressed parseOutput<Output::InputAddressed>(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings &)
{
    if (!hashAlgoStr.empty())
        throw FormatError("input-addressed derivation output should not specify a hash algorithm");
    if (!hashS.empty())
        throw FormatError("input-addressed derivation output should not specify a hash");
    return Output::InputAddressed{
        .path = store.parseStorePathCanonical(pathS),
    };
}

template<>
Output::CAFixed parseOutput<Output::CAFixed>(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    auto [method, hashAlgo] = parseCaMethodAlgo(hashAlgoStr, xpSettings);
    if (hashS.empty())
        throw FormatError("fixed-output derivation output must specify a hash");
    if (hashS == "impure"sv)
        throw FormatError("fixed-output derivation output must not be marked 'impure'");
    auto path = store.parseStorePathCanonical(pathS);
    Output::CAFixed dof{
        .ca =
            ContentAddress{
                .method = std::move(method),
                .hash = Hash::parseNonSRIUnprefixed(hashS, hashAlgo),
            },
    };
    /* The stated path is redundant --- it is a function of the
       content address --- but it must still agree, lest two
       derivations that mean the same thing hash differently.

       Skipped when fuzzing: the check makes the path a preimage
       of a hash of the rest of the output, which a fuzzer has no
       way to solve, so leaving it in would make this branch
       unreachable to it. `CAFixedPathMismatch` covers the check
       itself. See "Checks that defeat fuzzing" in
       doc/manual/source/development/testing.md. */
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    if (path != dof.path(store, drvName, outputName))
        throw FormatError(
            "derivation output '%s' has path '%s', which does not match its content address", outputName, pathS);
#else
    (void) path;
#endif
    return dof;
}

template<>
Output::CAFloating parseOutput<Output::CAFloating>(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto [method, hashAlgo] = parseCaMethodAlgo(hashAlgoStr, xpSettings);
    if (!hashS.empty())
        throw FormatError("floating content-addressing derivation output should not specify a hash");
    xpSettings.require(Xp::CaDerivations);
    if (!pathS.empty())
        throw FormatError("content-addressing derivation output should not specify output path");
    return Output::CAFloating{
        .method = std::move(method),
        .hashAlgo = std::move(hashAlgo),
    };
}

template<>
Output::Impure parseOutput<Output::Impure>(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    auto [method, hashAlgo] = parseCaMethodAlgo(hashAlgoStr, xpSettings);
    if (hashS != "impure"sv)
        throw FormatError("impure derivation output must be marked 'impure'");
    xpSettings.require(Xp::ImpureDerivations);
    if (!pathS.empty())
        throw FormatError("impure derivation output should not specify output path");
    return Output::Impure{
        .method = std::move(method),
        .hashAlgo = std::move(hashAlgo),
    };
}

template<>
Output parseOutput<Output>(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    if (!hashAlgoStr.empty()) {
        if (hashS == "impure"sv)
            return parseOutput<Output::Impure>(store, drvName, outputName, pathS, hashAlgoStr, hashS, xpSettings);
        else if (!hashS.empty())
            return parseOutput<Output::CAFixed>(store, drvName, outputName, pathS, hashAlgoStr, hashS, xpSettings);
        else
            return parseOutput<Output::CAFloating>(store, drvName, outputName, pathS, hashAlgoStr, hashS, xpSettings);
    } else if (pathS.empty()) {
        return parseOutput<Output::Deferred>(store, drvName, outputName, pathS, hashAlgoStr, hashS, xpSettings);
    } else {
        return parseOutput<Output::InputAddressed>(store, drvName, outputName, pathS, hashAlgoStr, hashS, xpSettings);
    }
}

/**
 * All ATerm Derivation format versions currently known.
 *
 * Unknown versions are rejected at the parsing stage.
 */
enum struct ATermVersion {
    /**
     * Older unversioned form
     */
    Traditional,

    /**
     * Newer versioned form; only this version so far.
     */
    DynamicDerivations,
};

static DerivedPathMap<StringSet>::ChildNode
parseDerivedPathMapNode(const StoreDirConfig & store, StringViewStream & str, ATermVersion version)
{
    using namespace std::literals::string_view_literals;

    DerivedPathMap<StringSet>::ChildNode node;

    auto parseNonDynamic = [&]() { node.value = parseStrings(str); };

    // Older derivation should never use new form, but newer
    // derivation can use old form.
    switch (version) {
    case ATermVersion::Traditional:
        parseNonDynamic();
        break;
    case ATermVersion::DynamicDerivations:
        switch (str.peek()) {
        case '[':
            parseNonDynamic();
            break;
        case '(':
            expect(str, '(');
            node.value = parseStrings(str);
            expect(str, ',');
            parseMap(
                str,
                node.childMap,
                [&] { return parseString(str).toOwned(); },
                [&](const auto &) { return parseDerivedPathMapNode(store, str, version); },
                [](const auto & outputName) { return fmt("output name '%s'", outputName); });
            expect(str, ')');
            break;
        default:
            throw FormatError("invalid inputDrvs entry in derivation");
        }
        break;
    default:
        // invalid format, not a parse error but internal error
        assert(false);
    }
    return node;
}

/**
 * The inverse of `unparseDerivedPathMapNode`, for each node type an
 * `inputDrvs` map has. The masked form has no nesting, so its version
 * argument goes unused.
 */
template<typename Node>
static Node parseNode(const StoreDirConfig & store, StringViewStream & str, ATermVersion version);

template<>
DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode
parseNode<DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode>(
    const StoreDirConfig & store, StringViewStream & str, ATermVersion version)
{
    return parseDerivedPathMapNode(store, str, version);
}

template<>
std::set<OutputName, std::less<>>
parseNode<std::set<OutputName, std::less<>>>(const StoreDirConfig &, StringViewStream & str, ATermVersion)
{
    auto outputNames = parseStrings(str);
    return {outputNames.begin(), outputNames.end()};
}

static bool nodeIsEmpty(const DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode & node)
{
    return node.value.empty() && node.childMap.empty();
}

/**
 * This one, unlike the public ones, is polymorphic on the input and
 * output parameters, to support the hash modulo intermediate form. It
 * is the inverse of `unparseDerivation`, and constrained by the same
 * concept.
 *
 * The type-specific parts --- how an `inputDrvs` key is written, what a
 * node under it looks like, which output alternatives are admissible
 * --- are the `parseKey`, `parseNode` and `parseOutput`
 * specializations above, mirroring `keyToString`,
 * `unparseDerivedPathMapNode` and `unparseOutput` on the printing side.
 */
template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
static Derivation<Inputs, Out> parseDerivation(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    Derivation<Inputs, Out> drv{
        .name = std::string{name},
    };

    StringViewStream str{s};
    expect(str, 'D');
    ATermVersion version;
    switch (str.peek()) {
    case 'e':
        expect(str, "erive("sv);
        version = ATermVersion::Traditional;
        break;
    case 'r': {
        /* The masked form is constructed only after dynamic
           derivations have been resolved away, so it never carries a
           version header. */
        if constexpr (!std::is_same_v<Inputs, FullInputs>)
            throw FormatError("masked derivation must not be versioned");
        else {
            expect(str, "rvWithVersion("sv);
            auto versionS = parseString(str);
            if (*versionS == "xp-dyn-drv"sv) {
                // Only version we have so far
                version = ATermVersion::DynamicDerivations;
                xpSettings.require(Xp::DynamicDerivations, [&] {
                    return fmt("derivation '%s', ATerm format version 'xp-dyn-drv'", name);
                });
            } else {
                throw FormatError("Unknown derivation ATerm format version '%s'", *versionS);
            }
            expect(str, ',');
        }
        break;
    }
    default:
        throw Error("derivation does not start with 'Derive' or 'DrvWithVersion'");
    }

    /* Parse the map of outputs. The value is three fields rather than
       one, but the framing is a map's, so `parseMap` still applies.

       Which alternatives an output may be is decided by `Out`, the
       output type this derivation shape carries. */
    parseMap(
        str,
        drv.outputs,
        [&] { return parseUnquotedString(str).toOwned(); },
        [&](const auto & outputName) {
            const auto pathS = parseStorePathString(str, supportWindowsStoreDir);
            expect(str, ',');
            const auto hashAlgo = parseString(str);
            expect(str, ',');
            const auto hash = parseString(str);
            return parseOutput<Out>(store, name, outputName, *pathS, *hashAlgo, *hash, xpSettings);
        },
        [](const auto & outputName) { return fmt("output name '%s'", outputName); });

    /* Parse the list of input derivations. */
    using DrvMap = std::remove_reference_t<decltype(drv.inputs.drvs.map)>;
    expect(str, ',');
    parseMap(
        str,
        drv.inputs.drvs.map,
        [&] { return parseKey<std::remove_const_t<typename DrvMap::key_type>>(store, str, supportWindowsStoreDir); },
        [&](const auto & key) {
            auto node = parseNode<typename DrvMap::mapped_type>(store, str, version);
            if (nodeIsEmpty(node))
                throw FormatError("inputDrvs entry for '%s' specifies no outputs", keyToString(store, key));
            return node;
        },
        [&](const auto & key) { return fmt("input derivation '%s'", keyToString(store, key)); });

    expect(str, ',');
    drv.inputs.srcs = parseStorePaths(store, str, supportWindowsStoreDir);
    expect(str, ',');
    drv.platform = parseUnquotedString(str).toOwned();
    expect(str, ',');
    drv.builder = parseString(str).toOwned();

    /* Parse the builder arguments. */
    expect(str, ',');
    parseList(str, [&] { drv.args.push_back(parseString(str).toOwned()); });

    /* Parse the environment variables. */
    expect(str, ',');
    parseMap(
        str,
        drv.env,
        [&] { return parseString(str).toOwned(); },
        [&](const auto &) { return parseString(str).toOwned(); },
        [](const auto & name) { return fmt("environment variable '%s'", name); });

    /* Structured attrs are just an ordinary environment variable as far
       as the ATerm is concerned, so only take them out once the whole
       map is parsed, and the ordering checks have seen them. */
    drv.structuredAttrs = StructuredAttrs::tryExtract(drv.env);

    expect(str, ')');
    if (!str.remaining.empty())
        throw FormatError("expected end of file, found '%s'", str.remaining);
    return drv;
}

template<typename Inputs, typename Out>
    requires ParsableDerivation<Inputs, Out>
Derivation<Inputs, Out> parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings & xpSettings)
{
    /* The flat set of inputs is what callers of the regular form want,
       but it is not what the ATerm holds; parse the nested form and
       then flatten. The masked form is already flat. */
    if constexpr (std::is_same_v<Inputs, std::set<SingleDerivedPath>>)
        return parseDerivation<FullInputs, Out>(store, std::move(s), name, supportWindowsStoreDir, xpSettings)
            .mapInputs([](const FullInputs & inputs) { return inputs.toSet(); });
    else
        return parseDerivation<Inputs, Out>(store, std::move(s), name, supportWindowsStoreDir, xpSettings);
}

template Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);
template Derivation<masked::HashInputs, Output::Deferred> parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);

/* --------------------------------------------------------------------------
   ATerm unparsing
   -------------------------------------------------------------------------- */

/**
 * Print a derivation string literal to an `std::string`.
 *
 * This syntax does not generalize to the expression language, which needs to
 * escape `$`.
 *
 * @param res Where to print to
 * @param s Which logical string to print
 */
static void printString(std::string & res, std::string_view s)
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

static void printUnquotedString(std::string & res, std::string_view s)
{
    res += '"';
    res.append(s);
    res += '"';
}

/**
 * Print a `[...]`-delimited list, calling `printItem` for each item.
 */
static void printList(std::string & res, const auto & items, auto printItem)
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
static void printMap(std::string & res, const auto & map, auto printKey, auto printValue)
{
    printList(res, map, [&](const auto & entry) {
        res += '(';
        printKey(entry.first);
        res += ',';
        printValue(entry.first, entry.second);
        res += ')';
    });
}

static void printStrings(std::string & res, const auto & strings)
{
    printList(res, strings, [&](const auto & s) { printString(res, s); });
}

static void printUnquotedStrings(std::string & res, const auto & strings)
{
    printList(res, strings, [&](const auto & s) { printUnquotedString(res, s); });
}

/* The counterpart of `parseStorePathString`. */
static void printStorePathString(std::string & res, std::string_view pathS, bool supportWindowsStoreDir)
{
    if (supportWindowsStoreDir)
        printString(res, pathS);
    else
        printUnquotedString(res, pathS);
}

static void
printStorePath(const StoreDirConfig & store, std::string & res, const StorePath & path, bool supportWindowsStoreDir)
{
    printStorePathString(res, store.printStorePath(path), supportWindowsStoreDir);
}

static void printStorePaths(
    const StoreDirConfig & store, std::string & res, const StorePathSet & paths, bool supportWindowsStoreDir)
{
    printList(res, paths, [&](const auto & path) { printStorePath(store, res, path, supportWindowsStoreDir); });
}

static void unparseDerivedPathMapNode(
    const StoreDirConfig &, std::string & s, const std::set<OutputName, std::less<>> & outputNames)
{
    printUnquotedStrings(s, outputNames);
}

static void unparseDerivedPathMapNode(
    const StoreDirConfig & store, std::string & s, const DerivedPathMap<StringSet>::ChildNode & node)
{
    if (node.childMap.empty()) {
        printUnquotedStrings(s, node.value);
    } else {
        s += '(';
        printUnquotedStrings(s, node.value);
        s += ',';
        printMap(
            s,
            node.childMap,
            [&](const auto & outputName) { printUnquotedString(s, outputName); },
            [&](const auto &, const auto & childNode) { unparseDerivedPathMapNode(store, s, childNode); });
        s += ')';
    }
}

static void
printKey(const StoreDirConfig & store, std::string & res, const StorePath & key, bool supportWindowsStoreDir)
{
    printStorePath(store, res, key, supportWindowsStoreDir);
}

static void printKey(const StoreDirConfig &, std::string & res, const Hash & key, bool)
{
    printUnquotedString(res, key.to_string(HashFormat::Base16, false));
}

/* The rendering `printKey` does, without the ATerm quoting, for error
   messages about a bad `inputDrvs` entry. */
static std::string keyToString(const StoreDirConfig & store, const StorePath & key)
{
    return store.printStorePath(key);
}

static std::string keyToString(const StoreDirConfig &, const Hash & key)
{
    return key.to_string(HashFormat::Base16, false);
}

static void unparseOutput(
    const StoreDirConfig & store,
    std::string & s,
    const Output::InputAddressed & doi,
    std::string_view,
    std::string_view,
    bool supportWindowsStoreDir)
{
    printStorePath(store, s, doi.path, supportWindowsStoreDir);
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
}

static void unparseOutput(
    const StoreDirConfig & store,
    std::string & s,
    const Output::CAFixed & dof,
    std::string_view drvName,
    std::string_view outputName,
    bool supportWindowsStoreDir)
{
    printStorePath(store, s, dof.path(store, drvName, outputName), supportWindowsStoreDir);
    s += ',';
    printUnquotedString(s, dof.ca.printMethodAlgo());
    s += ',';
    printUnquotedString(s, dof.ca.hash.to_string(HashFormat::Base16, false));
}

static void unparseOutput(
    const StoreDirConfig &,
    std::string & s,
    const Output::CAFloating & dof,
    std::string_view,
    std::string_view,
    bool supportWindowsStoreDir)
{
    printStorePathString(s, {}, supportWindowsStoreDir);
    s += ',';
    printUnquotedString(s, std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo));
    s += ',';
    printUnquotedString(s, {});
}

static void unparseOutput(
    const StoreDirConfig &,
    std::string & s,
    const Output::Deferred &,
    std::string_view,
    std::string_view,
    bool supportWindowsStoreDir)
{
    printStorePathString(s, {}, supportWindowsStoreDir);
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
}

static void unparseOutput(
    const StoreDirConfig &,
    std::string & s,
    const Output::Impure & doi,
    std::string_view,
    std::string_view,
    bool supportWindowsStoreDir)
{
    using namespace std::literals::string_view_literals;

    // FIXME
    printStorePathString(s, {}, supportWindowsStoreDir);
    s += ',';
    printUnquotedString(s, std::string{doi.method.renderPrefix()} + printHashAlgo(doi.hashAlgo));
    s += ',';
    printUnquotedString(s, "impure"sv);
}

static void unparseOutput(
    const StoreDirConfig & store,
    std::string & s,
    const Output & output,
    std::string_view drvName,
    std::string_view outputName,
    bool supportWindowsStoreDir)
{
    std::visit(
        [&](const auto & o) { unparseOutput(store, s, o, drvName, outputName, supportWindowsStoreDir); }, output.raw);
}

/**
 * This one, unlike the public one, is polymorphic on the output parameter to
 * support the hash modulo intermediate form.
 */
template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
std::string unparse(const Derivation<Inputs, Out> & drv, const StoreDirConfig & store, bool supportWindowsStoreDir)
{
    using namespace std::literals::string_view_literals;

    std::string s;
    s.reserve(65536);

    /* Use older unversioned form if possible, for wider compat. Use
       newer form only if we need it, which we do for
       `Xp::DynamicDerivations`. (The hash modulo intermediate form
       never has dynamic inputs; they are resolved away before it is
       constructed.) */
    bool dynDrvDep = false;
    if constexpr (std::is_same_v<Inputs, FullInputs>)
        dynDrvDep = hasDynamicDrvDep(drv.inputs);
    if (dynDrvDep) {
        s += "DrvWithVersion("sv;
        // Only version we have so far
        printUnquotedString(s, "xp-dyn-drv"sv);
        s += ',';
    } else {
        s += "Derive("sv;
    }

    printMap(
        s,
        drv.outputs,
        [&](const auto & outputName) { printUnquotedString(s, outputName); },
        [&](const auto & outputName, const auto & output) {
            unparseOutput(store, s, output, drv.name, outputName, supportWindowsStoreDir);
        });

    s += ',';
    printMap(
        s,
        drv.inputs.drvs.map,
        [&](const auto & key) { printKey(store, s, key, supportWindowsStoreDir); },
        [&](const auto &, const auto & node) { unparseDerivedPathMapNode(store, s, node); });

    s += ',';
    printStorePaths(store, s, drv.inputs.srcs, supportWindowsStoreDir);

    s += ',';
    printUnquotedString(s, drv.platform);
    s += ',';
    printString(s, drv.builder);
    s += ',';
    printStrings(s, drv.args);

    s += ',';

    auto unparseEnv = [&](const StringPairs & atermEnv) {
        printMap(
            s,
            atermEnv,
            [&](const auto & name) { printString(s, name); },
            [&](const auto &, const auto & value) { printString(s, value); });
    };

    StructuredAttrs::checkKeyNotInUse(drv.env);
    if (drv.structuredAttrs) {
        StringPairs scratch = drv.env;
        scratch.insert(drv.structuredAttrs->unparse());
        unparseEnv(scratch);
    } else {
        unparseEnv(drv.env);
    }

    s += ')';

    return s;
}

/* The hash modulo intermediate forms, unparsed by `masked.cc`. */
template std::string unparse(const masked::Drv<Output::Deferred> & drv, const StoreDirConfig & store, bool);
template std::string unparse(const masked::Drv<Output::InputAddressed> & drv, const StoreDirConfig & store, bool);

template<typename Out>
std::string unparse(
    const Derivation<std::set<SingleDerivedPath>, Out> & drv, const StoreDirConfig & store, bool supportWindowsStoreDir)
{
    // Convert to FullInputs for ATerm serialization
    return unparse(
        drv.mapInputs([](const std::set<SingleDerivedPath> & inputs) { return FullInputs::fromSet(inputs); }),
        store,
        supportWindowsStoreDir);
}

template std::string unparse(const Full & drv, const StoreDirConfig & store, bool);
template std::string unparse(const FullDeferred & drv, const StoreDirConfig & store, bool);
template std::string unparse(const FullInputAddressed & drv, const StoreDirConfig & store, bool);

/* --------------------------------------------------------------------------
   Wire protocol serialisation
   -------------------------------------------------------------------------- */

static Output readOutput(Source & in, const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseOutput<Output>(store, drvName, outputName, pathS, hashAlgo, hash, experimentalFeatureSettings);
}

Source & read(Source & in, const StoreDirConfig & store, Basic & drv, std::string_view name)
{
    drv.name = name;

    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto outputName = readString(in);
        auto output = readOutput(in, store, name, outputName);
        drv.outputs.emplace(std::move(outputName), std::move(output));
    }

    drv.inputs = CommonProto::Serialise<StorePathSet>::read(store, CommonProto::ReadConn{.from = in});
    in >> drv.platform >> drv.builder;
    drv.args = readStrings<Strings>(in);

    nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto key = readString(in);
        auto value = readString(in);
        drv.env[key] = value;
    }
    drv.structuredAttrs = StructuredAttrs::tryExtract(drv.env);

    return in;
}

void write(Sink & out, const StoreDirConfig & store, const Basic & drv)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs) {
        out << i.first;
        std::visit(
            overloaded{
                [&](const Output::InputAddressed & doi) {
                    out << store.printStorePath(doi.path) << ""
                        << "";
                },
                [&](const Output::CAFixed & dof) {
                    out << store.printStorePath(dof.path(store, drv.name, i.first)) << dof.ca.printMethodAlgo()
                        << dof.ca.hash.to_string(HashFormat::Base16, false);
                },
                [&](const Output::CAFloating & dof) {
                    out << "" << (std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo)) << "";
                },
                [&](const Output::Deferred &) {
                    out << ""
                        << ""
                        << "";
                },
                [&](const Output::Impure & doi) {
                    out << "" << (std::string{doi.method.renderPrefix()} + printHashAlgo(doi.hashAlgo)) << "impure";
                },
            },
            i.second.raw);
    }
    CommonProto::write(store, CommonProto::WriteConn{.to = out}, drv.inputs);
    out << drv.platform << drv.builder << drv.args;

    auto writeEnv = [&](const StringPairs atermEnv) {
        out << atermEnv.size();
        for (auto & [k, v] : atermEnv)
            out << k << v;
    };

    StructuredAttrs::checkKeyNotInUse(drv.env);
    if (drv.structuredAttrs) {
        StringPairs scratch = drv.env;
        scratch.insert(drv.structuredAttrs->unparse());
        writeEnv(scratch);
    } else {
        writeEnv(drv.env);
    }
}

} // namespace derivation

} // namespace nix
