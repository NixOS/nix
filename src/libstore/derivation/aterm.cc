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
   ATerm parser and printer pairs
   -------------------------------------------------------------------------- */

/**
 * The ATerm counterpart to `nlohmann::adl_serializer`: for each type
 * the format renders, the parser and the printer sit together in one
 * specialization, so that neither can be changed without the other
 * being right there.
 *
 * The members differ from family to family --- an output is three
 * fields inside a map entry, a key is a single token, a derivation is
 * a whole string --- because what the format asks of each is
 * different. What they have in common is that every type the format
 * can render has exactly one of these, and only the types the format
 * can render have one: the primary template is left undefined, so an
 * inadmissible shape is a compile error rather than a missing overload.
 */
template<typename T>
struct ATermSerializer;

/* Defined with the other printers below; declared here because the
   key serializers write their keys with it. */
static void printStorePathString(std::string & res, std::string_view pathS, bool supportWindowsStoreDir);

/* One macro per family, in the manner of `JSON_IMPL`: what the members
   are is a property of the family, so only the type varies. */

/**
 * A key of the `inputDrvs` map.
 *
 * Only `parse` and `toString` vary by key type. `toString` is the
 * rendering itself, which is also what an error message about a bad
 * entry wants; writing it is the same for every key --- a token,
 * escaped only where the store directory may contain backslashes ---
 * so `unparse` is given once, here.
 *
 * For a hash that distinction never bites, hex having nothing to
 * escape, and its parser accepts no escapes at all, so that each hash
 * has exactly one spelling.
 */
#define ATERM_IMPL_KEY(TYPE)                                                                                    \
    template<>                                                                                                  \
    struct ATermSerializer<TYPE>                                                                                \
    {                                                                                                           \
        static TYPE parse(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir);   \
        static std::string toString(const StoreDirConfig & store, const TYPE & key);                            \
                                                                                                                \
        static void                                                                                             \
        unparse(const StoreDirConfig & store, std::string & res, const TYPE & key, bool supportWindowsStoreDir) \
        {                                                                                                       \
            printStorePathString(res, toString(store, key), supportWindowsStoreDir);                            \
        }                                                                                                       \
    }

/**
 * The three fields an output is in the ATerm --- path, hash algorithm,
 * hash --- as written, before the alternative they spell out has been
 * decided or after it has been rendered. The wire protocol carries the
 * same three fields, unquoted, so it shares both conversions.
 *
 * `RawOutput` owns its fields, being what `unparse` produces;
 * `RawOutputView` borrows them, being what `parse` is given, straight
 * out of the text or the wire.
 */
struct RawOutput
{
    std::string path, hashAlgo, hash;
};

struct RawOutputView
{
    std::string_view path, hashAlgo, hash;
};

/**
 * An output, which is three fields within a map entry, so `parse` takes
 * them already separated rather than a stream, and `unparse` gives them
 * back separately, for the ATerm printer to quote or the wire protocol
 * to send as they are.
 */
#define ATERM_IMPL_OUTPUT(TYPE)                                                                                       \
    template<>                                                                                                        \
    struct ATermSerializer<TYPE>                                                                                      \
    {                                                                                                                 \
        static TYPE parse(                                                                                            \
            const StoreDirConfig & store,                                                                             \
            std::string_view drvName,                                                                                 \
            OutputNameView outputName,                                                                                \
            RawOutputView raw,                                                                                        \
            const ExperimentalFeatureSettings & xpSettings);                                                          \
                                                                                                                      \
        static RawOutput                                                                                              \
        unparse(const StoreDirConfig & store, const TYPE & o, std::string_view drvName, std::string_view outputName); \
    }

/**
 * A node under an `inputDrvs` key.
 *
 * `isEmpty` is here too because an entry with no outputs cannot be
 * represented in the flat inputs set, and so must be rejected rather
 * than silently dropped --- a question about the node, answered where
 * the node is read and written.
 */
#define ATERM_IMPL_NODE(TYPE)                                                                          \
    template<>                                                                                         \
    struct ATermSerializer<TYPE>                                                                       \
    {                                                                                                  \
        using Node = TYPE;                                                                             \
                                                                                                       \
        static Node parse(const StoreDirConfig & store, StringViewStream & str, ATermVersion version); \
        static void unparse(const StoreDirConfig & store, std::string & s, const Node & node);         \
        static bool isEmpty(const Node & node);                                                        \
    }

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
        throw FormatError("expected '%1%' character", c);
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

/* An `inputDrvs` key is a derivation path in the regular form, a hash
   in the masked one. `StorePath` is written the same way, and is what
   an input source and an output path are, so it gets one too --- the
   general case, with no `.drv` requirement. */
ATERM_IMPL_KEY(StorePath);
ATERM_IMPL_KEY(DerivationPath);
ATERM_IMPL_KEY(Hash);

StorePath
ATermSerializer<StorePath>::parse(const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    return parseStorePath(store, str, supportWindowsStoreDir);
}

DerivationPath ATermSerializer<DerivationPath>::parse(
    const StoreDirConfig & store, StringViewStream & str, bool supportWindowsStoreDir)
{
    return DerivationPath::require(ATermSerializer<StorePath>::parse(store, str, supportWindowsStoreDir));
}

Hash ATermSerializer<Hash>::parse(const StoreDirConfig & store, StringViewStream & str, bool)
{
    auto s = parseUnquotedString(str);
    auto hash = Hash::parseExplicitFormatUnprefixed(*s, HashAlgorithm::SHA256, HashFormat::Base16);
    /* Base-16 decoding also accepts upper-case digits, so check that
       this is the one spelling `toString` produces. A second spelling of
       the same masked derivation would print differently from how it
       was read, and hash differently too. */
    if (toString(store, hash) != *s)
        throw FormatError("input derivation hash '%s' is not in lower-case base-16", *s);
    return hash;
}

/**
 * The derivation itself: a whole ATerm string in, or out.
 *
 * A partial specialization rather than a pair of constrained function
 * templates, which is what lets the admissible shapes be spelled once,
 * here, instead of as a concept each direction has to repeat.
 *
 * The type-specific parts --- how an `inputDrvs` key is written, what a
 * node under it looks like, which output alternatives are admissible
 * --- are the serializers above, which this one defers to.
 */
template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
struct ATermSerializer<Derivation<Inputs, Out>>
{
    static Derivation<Inputs, Out> parse(
        const StoreDirConfig & store,
        std::string && s,
        std::string_view name,
        bool supportWindowsStoreDir,
        const ExperimentalFeatureSettings & xpSettings);

    static std::string
    unparse(const Derivation<Inputs, Out> & drv, const StoreDirConfig & store, bool supportWindowsStoreDir);
};

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
 * There is one specialization per alternative, and the `Output` one
 * dispatches on the syntax into them, just as its `unparse` dispatches
 * with `std::visit`. Each alternative validates all three fields
 * rather than trusting the dispatcher to have chosen correctly, so any
 * of them is safe to use on its own --- which is what the masked
 * forms, whose type names a single alternative, do.
 */
ATERM_IMPL_OUTPUT(Output::Deferred);
ATERM_IMPL_OUTPUT(Output::InputAddressed);
ATERM_IMPL_OUTPUT(Output::CAFixed);
ATERM_IMPL_OUTPUT(Output::CAFloating);
ATERM_IMPL_OUTPUT(Output::Impure);
ATERM_IMPL_OUTPUT(Output);

Output::Deferred ATermSerializer<Output::Deferred>::parse(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings &)
{
    auto [pathS, hashAlgoStr, hashS] = raw;
    if (!hashAlgoStr.empty())
        throw FormatError("deferred derivation output should not specify a hash algorithm");
    if (!hashS.empty())
        throw FormatError("deferred derivation output should not specify a hash");
    if (!pathS.empty())
        throw FormatError("deferred derivation output should not specify an output path");
    return {};
}

Output::InputAddressed ATermSerializer<Output::InputAddressed>::parse(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings &)
{
    auto [pathS, hashAlgoStr, hashS] = raw;
    if (!hashAlgoStr.empty())
        throw FormatError("input-addressed derivation output should not specify a hash algorithm");
    if (!hashS.empty())
        throw FormatError("input-addressed derivation output should not specify a hash");
    return Output::InputAddressed{
        .path = store.parseStorePathCanonical(pathS),
    };
}

Output::CAFixed ATermSerializer<Output::CAFixed>::parse(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    auto [pathS, hashAlgoStr, hashS] = raw;

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

Output::CAFloating ATermSerializer<Output::CAFloating>::parse(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto [pathS, hashAlgoStr, hashS] = raw;
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

Output::Impure ATermSerializer<Output::Impure>::parse(
    const StoreDirConfig &,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    auto [pathS, hashAlgoStr, hashS] = raw;

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

Output ATermSerializer<Output>::parse(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
    RawOutputView raw,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    /* A hash is only meaningful with the algorithm it was computed
       with. Said here, before dispatching, because no alternative
       would otherwise be chosen whose own error says so. */
    if (raw.hashAlgo.empty() && !raw.hash.empty())
        throw FormatError("hash specified without hash algorithm");

    if (!raw.hashAlgo.empty()) {
        if (raw.hash == "impure"sv)
            return ATermSerializer<Output::Impure>::parse(store, drvName, outputName, raw, xpSettings);
        else if (!raw.hash.empty())
            return ATermSerializer<Output::CAFixed>::parse(store, drvName, outputName, raw, xpSettings);
        else
            return ATermSerializer<Output::CAFloating>::parse(store, drvName, outputName, raw, xpSettings);
    } else if (raw.path.empty()) {
        return ATermSerializer<Output::Deferred>::parse(store, drvName, outputName, raw, xpSettings);
    } else {
        return ATermSerializer<Output::InputAddressed>::parse(store, drvName, outputName, raw, xpSettings);
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
 * The node under an `inputDrvs` key, for each node type such a map
 * has. The masked form has no nesting, so its version argument goes
 * unused.
 *
 * `isEmpty` is here too because an entry with no outputs cannot be
 * represented in the flat inputs set, and so must be rejected rather
 * than silently dropped --- a question about the node, answered where
 * the node is read and written.
 */
#define ARG DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode
ATERM_IMPL_NODE(ARG);
#undef ARG

#define ARG std::set<OutputName, std::less<>>
ATERM_IMPL_NODE(ARG);
#undef ARG

DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode
ATermSerializer<DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode>::parse(
    const StoreDirConfig & store, StringViewStream & str, ATermVersion version)
{
    return parseDerivedPathMapNode(store, str, version);
}

std::set<OutputName, std::less<>>
ATermSerializer<std::set<OutputName, std::less<>>>::parse(const StoreDirConfig &, StringViewStream & str, ATermVersion)
{
    auto outputNames = parseStrings(str);
    return {outputNames.begin(), outputNames.end()};
}

/**
 * An `inputDrvs` entry with no outputs cannot be represented in the
 * flat inputs set, and would thus be silently dropped rather than
 * round-tripped. Nix itself never produces one.
 */
bool ATermSerializer<std::set<OutputName, std::less<>>>::isEmpty(const Node & node)
{
    return node.empty();
}

bool ATermSerializer<DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode>::isEmpty(const Node & node)
{
    return node.value.empty() && node.childMap.empty();
}

template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
Derivation<Inputs, Out> ATermSerializer<Derivation<Inputs, Out>>::parse(
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
                throw FormatError("unknown derivation ATerm format version '%s'", *versionS);
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
            return ATermSerializer<Out>::parse(
                store,
                name,
                outputName,
                {
                    .path = *pathS,
                    .hashAlgo = *hashAlgo,
                    .hash = *hash,
                },
                xpSettings);
        },
        [](const auto & outputName) { return fmt("output name '%s'", outputName); });

    /* Parse the list of input derivations. */
    using DrvMap = std::remove_reference_t<decltype(drv.inputs.drvs.map)>;
    expect(str, ',');
    parseMap(
        str,
        drv.inputs.drvs.map,
        [&] {
            return ATermSerializer<std::remove_const_t<typename DrvMap::key_type>>::parse(
                store, str, supportWindowsStoreDir);
        },
        [&](const auto & key) {
            using NodeSerializer = ATermSerializer<typename DrvMap::mapped_type>;
            auto node = NodeSerializer::parse(store, str, version);
            if (NodeSerializer::isEmpty(node))
                throw FormatError(
                    "inputDrvs entry for '%s' specifies no outputs",
                    ATermSerializer<std::remove_const_t<typename DrvMap::key_type>>::toString(store, key));
            return node;
        },
        [&](const auto & key) {
            return fmt("input derivation '%s'", ATermSerializer<std::decay_t<decltype(key)>>::toString(store, key));
        });

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
        return ATermSerializer<Derivation<FullInputs, Out>>::parse(
                   store, std::move(s), name, supportWindowsStoreDir, xpSettings)
            .mapInputs([](const FullInputs & inputs) { return inputs.toSet(); });
    else
        return ATermSerializer<Derivation<Inputs, Out>>::parse(
            store, std::move(s), name, supportWindowsStoreDir, xpSettings);
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
template FullDeferred parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);
template FullInputAddressed parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);
template Derivation<masked::HashInputs, Output::InputAddressed> parse(
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

static void printStorePaths(
    const StoreDirConfig & store, std::string & res, const StorePathSet & paths, bool supportWindowsStoreDir)
{
    printList(res, paths, [&](const auto & path) {
        ATermSerializer<StorePath>::unparse(store, res, path, supportWindowsStoreDir);
    });
}

void ATermSerializer<std::set<OutputName, std::less<>>>::unparse(
    const StoreDirConfig &, std::string & s, const std::set<OutputName, std::less<>> & outputNames)
{
    printUnquotedStrings(s, outputNames);
}

void ATermSerializer<DerivedPathMap<StringSet>::ChildNode>::unparse(
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
            [&](const auto &, const auto & childNode) {
                ATermSerializer<std::decay_t<decltype(childNode)>>::unparse(store, s, childNode);
            });
        s += ')';
    }
}

std::string ATermSerializer<StorePath>::toString(const StoreDirConfig & store, const StorePath & key)
{
    return store.printStorePath(key);
}

std::string ATermSerializer<DerivationPath>::toString(const StoreDirConfig & store, const DerivationPath & key)
{
    return ATermSerializer<StorePath>::toString(store, key);
}

std::string ATermSerializer<Hash>::toString(const StoreDirConfig &, const Hash & key)
{
    return key.to_string(HashFormat::Base16, false);
}

RawOutput ATermSerializer<Output::InputAddressed>::unparse(
    const StoreDirConfig & store, const Output::InputAddressed & doi, std::string_view, std::string_view)
{
    return {
        .path = store.printStorePath(doi.path),
    };
}

RawOutput ATermSerializer<Output::CAFixed>::unparse(
    const StoreDirConfig & store, const Output::CAFixed & dof, std::string_view drvName, std::string_view outputName)
{
    return {
        .path = store.printStorePath(dof.path(store, drvName, outputName)),
        .hashAlgo = dof.ca.printMethodAlgo(),
        .hash = dof.ca.hash.to_string(HashFormat::Base16, false),
    };
}

RawOutput ATermSerializer<Output::CAFloating>::unparse(
    const StoreDirConfig &, const Output::CAFloating & dof, std::string_view, std::string_view)
{
    return {
        .hashAlgo = std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo),
    };
}

RawOutput ATermSerializer<Output::Deferred>::unparse(
    const StoreDirConfig &, const Output::Deferred &, std::string_view, std::string_view)
{
    return {};
}

RawOutput ATermSerializer<Output::Impure>::unparse(
    const StoreDirConfig &, const Output::Impure & doi, std::string_view, std::string_view)
{
    // FIXME
    return {
        .hashAlgo = std::string{doi.method.renderPrefix()} + printHashAlgo(doi.hashAlgo),
        .hash = "impure",
    };
}

RawOutput ATermSerializer<Output>::unparse(
    const StoreDirConfig & store, const Output & output, std::string_view drvName, std::string_view outputName)
{
    return std::visit(
        [&](const auto & o) {
            return ATermSerializer<std::decay_t<decltype(o)>>::unparse(store, o, drvName, outputName);
        },
        output.raw);
}

template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
std::string ATermSerializer<Derivation<Inputs, Out>>::unparse(
    const Derivation<Inputs, Out> & drv, const StoreDirConfig & store, bool supportWindowsStoreDir)
{
    using namespace std::literals::string_view_literals;

    std::string s;
    s.reserve(65536);

    /* Use older unversioned form if possible, for wider compat. Use
       newer form only if we need it, which we do for
       `Xp::DynamicDerivations`. (The masked hash intermediate form
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
            auto raw = ATermSerializer<Out>::unparse(store, output, drv.name, outputName);
            printStorePathString(s, raw.path, supportWindowsStoreDir);
            s += ',';
            printUnquotedString(s, raw.hashAlgo);
            s += ',';
            printUnquotedString(s, raw.hash);
        });

    s += ',';
    printMap(
        s,
        drv.inputs.drvs.map,
        [&](const auto & key) {
            ATermSerializer<std::decay_t<decltype(key)>>::unparse(store, s, key, supportWindowsStoreDir);
        },
        [&](const auto &, const auto & node) {
            ATermSerializer<std::decay_t<decltype(node)>>::unparse(store, s, node);
        });

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

/* The public entry point is a thin wrapper over the serializer, as
   `parse` is. */
template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
std::string unparse(const Derivation<Inputs, Out> & drv, const StoreDirConfig & store, bool supportWindowsStoreDir)
{
    return ATermSerializer<Derivation<Inputs, Out>>::unparse(drv, store, supportWindowsStoreDir);
}

/* The masked hash intermediate forms, unparsed by `masked.cc`. */
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

    return ATermSerializer<Output>::parse(
        store,
        drvName,
        outputName,
        {
            .path = pathS,
            .hashAlgo = hashAlgo,
            .hash = hash,
        },
        experimentalFeatureSettings);
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
        auto raw = ATermSerializer<Output>::unparse(store, i.second, drv.name, i.first);
        out << i.first << raw.path << raw.hashAlgo << raw.hash;
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
