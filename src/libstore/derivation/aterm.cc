#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/elaborate.hh"
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

constexpr struct Escapes
{
    char map[256];

    constexpr Escapes()
    {
        for (int i = 0; i < 256; i++)
            map[i] = (char) (unsigned char) i;
        map[(int) (unsigned char) 'n'] = '\n';
        map[(int) (unsigned char) 'r'] = '\r';
        map[(int) (unsigned char) 't'] = '\t';
    }

    char operator[](char c) const
    {
        return map[(unsigned char) c];
    }
} escapes;
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

/* Read a C-style string from stream `str'. */
static BackedStringView parseString(StringViewStream & str)
{
    expect(str, '"');
    size_t start = 0;
    size_t end = str.remaining.size();
    const auto data = str.remaining.data();
    bool foundClose = false;
    while (start < end) {
        auto idx = str.remaining.find('"', start);
        if (idx == std::string_view::npos) {
            break;
        }
        size_t pos = idx;
        for (; pos > 0 && data[pos - 1] == '\\'; pos--)
            ;
        if ((idx - pos) % 2 == 0) { // even number of backslashes
            end = idx;
            foundClose = true;
            break;
        }
        start = idx + 1;
    }
    if (!foundClose)
        throw FormatError("unterminated string in derivation");

    start = 0;
    const auto content = str.remaining.substr(start, end);
    str.remaining.remove_prefix(end + 1);

    auto nextBackslash = content.find('\\', start);
    if (nextBackslash == std::string_view::npos) {
        return content;
    }

    std::string res;
    res.reserve(end);
    do {
        if (nextBackslash == end - 1) {
            throw FormatError("unterminated string in derivation");
        }
        if (nextBackslash > start) {
            res.append(&data[start], nextBackslash - start);
        }
        res.push_back(escapes[data[nextBackslash + 1]]);
        start = nextBackslash + 2;
        nextBackslash = content.find('\\', start);
    } while (nextBackslash != std::string_view::npos);
    if (end > start) {
        res.append(&data[start], end - start);
    }
    return res;
}

static void validatePath(std::string_view s)
{
    if (s.size() == 0 || s[0] != '/')
        throw FormatError("bad path '%1%' in derivation", s);
}

static BackedStringView parsePath(StringViewStream & str)
{
    auto s = parseString(str);
    validatePath(*s);
    return s;
}

static bool endOfList(StringViewStream & str)
{
    if (str.peek() == ',') {
        str.get();
        return false;
    }
    if (str.peek() == ']') {
        str.get();
        return true;
    }
    return false;
}

static StringSet parseStrings(StringViewStream & str, bool arePaths)
{
    StringSet res;
    expect(str, '[');
    while (!endOfList(str))
        res.insert((arePaths ? parsePath(str) : parseString(str)).toOwned());
    return res;
}

static Output parseOutput(
    const StoreDirConfig & store,
    std::string_view pathS,
    std::string_view hashAlgoStr,
    std::string_view hashS,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    if (!hashAlgoStr.empty()) {
        ContentAddressMethod method = ContentAddressMethod::parsePrefix(hashAlgoStr);
        if (method == ContentAddressMethod::Raw::Text)
            xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output");
        const auto hashAlgo = parseHashAlgo(hashAlgoStr);
        if (hashS == "impure"sv) {
            xpSettings.require(Xp::ImpureDerivations);
            if (!pathS.empty())
                throw FormatError("impure derivation output should not specify output path");
            return Output::Impure{
                .method = std::move(method),
                .hashAlgo = std::move(hashAlgo),
            };
        } else if (!hashS.empty()) {
            validatePath(pathS);
            auto hash = Hash::parseNonSRIUnprefixed(hashS, hashAlgo);
            return Output::CAFixed{
                .ca =
                    ContentAddress{
                        .method = std::move(method),
                        .hash = std::move(hash),
                    },
            };
        } else {
            xpSettings.require(Xp::CaDerivations);
            if (!pathS.empty())
                throw FormatError("content-addressing derivation output should not specify output path");
            return Output::CAFloating{
                .method = std::move(method),
                .hashAlgo = std::move(hashAlgo),
            };
        }
    } else {
        if (pathS.empty()) {
            return Output::Deferred{};
        }
        validatePath(pathS);
        return Output::InputAddressed{
            .path = store.parseStorePath(pathS),
        };
    }
}

static Output parseOutput(
    const StoreDirConfig & store,
    StringViewStream & str,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings)
{
    expect(str, ',');
    const auto pathS = parseString(str);
    expect(str, ',');
    const auto hashAlgo = parseString(str);
    expect(str, ',');
    const auto hash = parseString(str);
    expect(str, ')');

    return parseOutput(store, *pathS, *hashAlgo, *hash, xpSettings);
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

    auto parseNonDynamic = [&]() { node.value = parseStrings(str, false); };

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
            node.value = parseStrings(str, false);
            expect(str, ",["sv);
            while (!endOfList(str)) {
                expect(str, '(');
                auto outputName = parseString(str).toOwned();
                expect(str, ',');
                node.childMap.insert_or_assign(outputName, parseDerivedPathMapNode(store, str, version));
                expect(str, ')');
            }
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

template<>
ATerm ATerm::parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    ATerm drv;

    StringViewStream str{s};
    expect(str, 'D');
    ATermVersion version;
    switch (str.peek()) {
    case 'e':
        expect(str, "erive("sv);
        version = ATermVersion::Traditional;
        break;
    case 'r': {
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
        break;
    }
    default:
        throw Error("derivation does not start with 'Derive' or 'DrvWithVersion'");
    }

    /* Parse the list of outputs. */
    expect(str, '[');
    while (!endOfList(str)) {
        expect(str, '(');
        std::string id = parseString(str).toOwned();
        auto output = parseOutput(store, str, xpSettings);
        drv.outputs.emplace(std::move(id), std::move(output));
    }

    /* Parse the list of input derivations. */
    derivation::FullInputs fullInputs;
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto drvPath = parsePath(str);
        expect(str, ',');
        auto node = parseDerivedPathMapNode(store, str, version);
        /* Such an entry cannot be represented in the flat inputs set,
           and would thus be silently dropped rather than round-tripped.
           Nix itself never produces one. */
        if (node.value.empty() && node.childMap.empty())
            throw FormatError("inputDrvs entry for '%s' specifies no outputs", *drvPath);
        fullInputs.drvs.map.insert_or_assign(store.parseStorePath(*drvPath), std::move(node));
        expect(str, ')');
    }

    expect(str, ',');
    fullInputs.srcs = store.parseStorePathSet(parseStrings(str, true));
    drv.inputs = std::move(fullInputs);
    expect(str, ',');
    drv.platform = parseString(str).toOwned();
    expect(str, ',');
    drv.builder = parseString(str).toOwned();

    /* Parse the builder arguments. */
    expect(str, ",["sv);
    while (!endOfList(str))
        drv.args.push_back(parseString(str).toOwned());

    /* Parse the environment variables, kept verbatim. */
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto name = parseString(str).toOwned();
        expect(str, ',');
        auto value = parseString(str);
        drv.env.insert_or_assign(std::move(name), std::move(value).toOwned());
        expect(str, ')');
    }

    expect(str, ')');

    return drv;
}

Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    return ATerm::parse(store, std::move(s), name, xpSettings).elaborate(store, name, xpSettings);
}

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
        for (auto c : chunk)
            if (c == '\"' || c == '\\') {
                *p++ = '\\';
                *p++ = c;
            } else if (c == '\n') {
                *p++ = '\\';
                *p++ = 'n';
            } else if (c == '\r') {
                *p++ = '\\';
                *p++ = 'r';
            } else if (c == '\t') {
                *p++ = '\\';
                *p++ = 't';
            } else
                *p++ = c;
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

template<class ForwardIterator>
static void printStrings(std::string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for (; i != j; ++i) {
        if (first)
            first = false;
        else
            res += ',';
        printString(res, *i);
    }
    res += ']';
}

template<class ForwardIterator>
static void printUnquotedStrings(std::string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for (; i != j; ++i) {
        if (first)
            first = false;
        else
            res += ',';
        printUnquotedString(res, *i);
    }
    res += ']';
}

static void unparseDerivedPathMapNode(
    const StoreDirConfig &, std::string & s, const std::set<OutputName, std::less<>> & outputNames)
{
    s += ',';
    printUnquotedStrings(s, outputNames.begin(), outputNames.end());
}

static void unparseDerivedPathMapNode(
    const StoreDirConfig & store, std::string & s, const DerivedPathMap<StringSet>::ChildNode & node)
{
    using namespace std::literals::string_view_literals;

    s += ',';
    if (node.childMap.empty()) {
        printUnquotedStrings(s, node.value.begin(), node.value.end());
    } else {
        s += '(';
        printUnquotedStrings(s, node.value.begin(), node.value.end());
        s += ",["sv;
        bool first = true;
        for (auto & [outputName, childNode] : node.childMap) {
            if (first)
                first = false;
            else
                s += ',';
            s += '(';
            printUnquotedString(s, outputName);
            unparseDerivedPathMapNode(store, s, childNode);
            s += ')';
        }
        s += "])"sv;
    }
}

/**
 * Inputs in the intermediate form used to compute the hash modulo:
 * input derivations are identified by their hash modulo rather than by
 * store path.
 *
 * `Hash::operator<=>` compares bytes left-to-right, which matches
 * base16-lexicographic order (hex encoding is monotonic per byte), so
 * `std::map<Hash, ...>` gives the correct ATerm key ordering directly.
 */
struct HashModuloInputs
{
    StorePathSet srcs;

    /**
     * No `DerivedPathMap` involved: the hash modulo is only ever
     * computed after dynamic inputs are resolved away, so each input
     * derivation maps to a plain set of output names.
     */
    using DrvMap = std::map<Hash, std::set<OutputName, std::less<>>>;

    /**
     * Nesting just to match `DerivedPathMap` for easier templating.
     */
    struct
    {
        DrvMap map;
    } drvs;

    // no operator== needed; this type is internal-only
};

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
    std::string_view)
{
    s += ',';
    printUnquotedString(s, store.printStorePath(doi.path));
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
    std::string_view outputName)
{
    s += ',';
    printUnquotedString(s, store.printStorePath(dof.path(store, drvName, outputName)));
    s += ',';
    printUnquotedString(s, dof.ca.printMethodAlgo());
    s += ',';
    printUnquotedString(s, dof.ca.hash.to_string(HashFormat::Base16, false));
}

static void unparseOutput(
    const StoreDirConfig &, std::string & s, const Output::CAFloating & dof, std::string_view, std::string_view)
{
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo));
    s += ',';
    printUnquotedString(s, {});
}

static void
unparseOutput(const StoreDirConfig &, std::string & s, const Output::Deferred &, std::string_view, std::string_view)
{
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
}

static void
unparseOutput(const StoreDirConfig &, std::string & s, const Output::Impure & doi, std::string_view, std::string_view)
{
    using namespace std::literals::string_view_literals;

    // FIXME
    s += ',';
    printUnquotedString(s, {});
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
    std::string_view outputName)
{
    std::visit([&](const auto & o) { unparseOutput(store, s, o, drvName, outputName); }, output.raw);
}

/**
 * This one, unlike the public one, is polymorphic on the output parameter to
 * support the (private) `hashInputModulo`.
 *
 * The `inputs` to serialize are passed separately, in one of the
 * format-oriented representations (`FullInputs` or `HashModuloInputs`);
 * `drv.inputs` itself is ignored.
 */
template<
    typename Inputs,
    typename Drv,
    typename Out = typename std::decay_t<decltype(std::declval<Drv>().outputs)>::mapped_type>
static std::string
unparseDerivation(const StoreDirConfig & store, const Inputs & inputs, std::string_view drvName, const Drv & drv)
    requires(
        // Regular `FullInputs` case must have regular `Output` outputs
        (std::is_same_v<Inputs, FullInputs> && std::is_same_v<Out, Output>)
        // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
        || (std::is_same_v<Inputs, HashModuloInputs>
            && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) ))
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
        dynDrvDep = hasDynamicDrvDep(inputs);
    if (dynDrvDep) {
        s += "DrvWithVersion("sv;
        // Only version we have so far
        printUnquotedString(s, "xp-dyn-drv"sv);
        s += ',';
    } else {
        s += "Derive("sv;
    }

    bool first = true;
    s += '[';
    for (auto & i : drv.outputs) {
        if (first)
            first = false;
        else
            s += ',';
        s += '(';
        printUnquotedString(s, i.first);
        unparseOutput(store, s, i.second, drvName, i.first);
        s += ')';
    }

    s += "],["sv;
    first = true;
    for (auto & [key, node] : inputs.drvs.map) {
        if (first)
            first = false;
        else
            s += ',';
        s += '(';
        printUnquotedString(s, keyToString(store, key));
        unparseDerivedPathMapNode(store, s, node);
        s += ')';
    }

    s += "],"sv;
    auto paths = store.printStorePathSet(inputs.srcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ',';
    printUnquotedString(s, drv.platform);
    s += ',';
    printString(s, drv.builder);
    s += ',';
    printStrings(s, drv.args.begin(), drv.args.end());

    s += ",["sv;
    first = true;

    for (auto & i : drv.env) {
        if (first)
            first = false;
        else
            s += ',';
        s += '(';
        printString(s, i.first);
        s += ',';
        printString(s, i.second);
        s += ')';
    }

    s += "])"sv;

    return s;
}

template<>
std::string ATerm::to_string(const StoreDirConfig & store, std::string_view name) const
{
    return unparseDerivation(store, inputs, name, *this);
}

template<typename Inputs, typename Out>
ATermT<Inputs, Out> ATermT<Inputs, Out>::lower(const Elaborated & drv)
{
    StringPairs env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);
    StructuredAttrs::checkKeyNotInUse(env);
    if (drv.structuredAttrs)
        env.insert(drv.structuredAttrs->unparse());

    Outputs<Out> outputs;
    for (auto & [outputName, output] : drv.outputs)
        outputs.insert_or_assign(outputName, output.output);

    ATermT<Inputs, Out> res{
        .outputs = std::move(outputs),
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = std::move(env),
    };
    if constexpr (std::is_same_v<Inputs, FullInputs>)
        res.inputs = FullInputs::fromSet(drv.inputs);
    else
        res.inputs = drv.inputs;
    return res;
}

template ATerm ATerm::lower(const Full & drv);
template BasicATerm BasicATerm::lower(const Basic & drv);

std::string unparse(const Full & drv, const StoreDirConfig & store)
{
    auto lowered = ATerm::lower(drv);

    /* Lowering to the ATerm format drops the parsed options and
       structured attributes; it is only faithful if re-parsing the
       environment gets them back. This is the case for all derivations
       whose options actually stem from the legacy environment-variable
       encoding. */
    if (lowered.elaborate(store, drv.name) != drv)
        throw Error(
            "derivation '%s' is not representable in the ATerm format: "
            "its fields are not in sync with their environment-variable encoding",
            drv.name);

    return lowered.to_string(store, drv.name);
}

/* --------------------------------------------------------------------------
   Derivation hash modulo
   -------------------------------------------------------------------------- */

Hashes hashes;

/* pathInputModulo and hashInputModulo are mutually recursive
 */

/**
 * Look up the derivation by value and memoize the `hashInputModulo` call.
 */
static HashModulo pathInputModulo(Store & store, const StorePath & drvPath)
{
    std::optional<HashModulo> hash;
    if (hashes.cvisit(drvPath, [&hash](const auto & kv) { hash.emplace(kv.second); })) {
        return *hash;
    }
    auto h = hashInputModulo(store, store.readInvalidDerivation(drvPath));

    // Cache it
    hashes.insert_or_assign(drvPath, h);
    return h;
}

/**
 * Look up the hash modulo for the input derivation at `drvPath` and
 * insert the result into `drvInputs`.
 *
 * Returns `true` if deferred and cannot mutate (the caller should bail out).
 */
static bool inputModulo(
    Store & store,
    HashModuloInputs::DrvMap & drvInputs,
    const StorePath & drvPath,
    const std::set<OutputName, std::less<>> & outputNames,
    std::string_view drvName)
{
    const auto & res = pathInputModulo(store, drvPath);
    return std::visit(
        overloaded{
            [&](const HashModulo::DeferredDrv &) { return true; },
            // Regular non-CA derivation, replace derivation
            [&](const HashModulo::DrvHash & drvHash) {
                drvInputs.insert_or_assign(drvHash, outputNames);
                return false;
            },
            // CA derivation's output hashes
            [&](const HashModulo::CaOutputHashes & outputHashes) {
                for (auto & outputName : outputNames) {
                    /* Put each one in with a single "out" output.. */
                    const auto h = get(outputHashes, outputName);
                    if (!h)
                        throw Error("no hash for output '%s' of derivation '%s'", outputName, drvName);
                    drvInputs.insert_or_assign(*h, std::set<OutputName, std::less<>>{"out"});
                }
                return false;
            },
        },
        res.raw);
}

/**
 * Replace each input derivation store path with its hash modulo,
 * producing the intermediate form used to compute the derivation hash.
 *
 * When `Out` is `DerivationOutput::Deferred`, outputs are masked:
 * output paths and matching env vars are blanked so the hash does not
 * depend on its own output paths.
 *
 * When `Out` is `DerivationOutput`, outputs are preserved as-is.
 *
 * Returns `std::nullopt` if any input is deferred (depends on a CA or
 * dynamic derivation whose outputs are not yet known).
 */
static std::optional<HashModuloInputs> derivationModulo(Store & store, FullInputs inputs, std::string_view drvName)
{
    HashModuloInputs masked{
        .srcs = std::move(inputs.srcs),
        .drvs = {},
    };

    for (auto & [drvPath, node] : inputs.drvs.map) {
        /* Need to build and resolve dynamic derivations first */
        if (!node.childMap.empty())
            return std::nullopt;
        if (inputModulo(store, masked.drvs.map, drvPath, node.value, drvName))
            return std::nullopt;
    }

    return masked;
}

/* See the header for interface details. These are the implementation details.

   For fixed-output derivations, each hash in the map is not the
   corresponding output's content hash, but a hash of that hash along
   with other constant data. The key point is that the value is a pure
   function of the output's contents, and there are no preimage attacks
   either spoofing an output's contents for a derivation, or
   spoofing a derivation for an output's contents.

   For regular derivations, it looks up each subderivation from its hash
   and recurs. If the subderivation is also regular, it simply
   substitutes the derivation path with its hash. If the subderivation
   is fixed-output, however, it takes each output hash and pretends it
   is a derivation hash producing a single "out" output. This is so we
   don't leak the provenance of fixed outputs, reducing pointless cache
   misses as the build itself won't know this.
 */

/**
 * Compute the hash with outputs preserved (as `InputAddressed`).
 * Used for computing a derivation's identity as an input to other
 * derivations.
 *
 * Returns the appropriate `HashModulo` variant:
 * - `CaOutputHashes` for fixed-output CA derivations
 * - `DeferredDrv` for deferred, non-fixed CA, or impure derivations
 * - `DrvHash` for regular input-addressed derivations
 */
HashModulo hashInputModulo(Store & store, const Full & drv)
{
    /* Return a fixed hash for fixed-output derivations. */
    if (type(drv).isFixed()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<Output::CAFixed>(i.second.output.raw);
            auto hash = hashString(
                HashAlgorithm::SHA256,
                "fixed:out:" + dof.ca.printMethodAlgo() + ":" + dof.ca.hash.to_string(HashFormat::Base16, false) + ":"
                    + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return outputHashes;
    }

    /* Extract InputAddressed outputs. If any output is not
       InputAddressed, this derivation has no hash modulo. */
    Outputs<Output::InputAddressed> convertedOutputs;
    for (auto & [name, output] : drv.outputs) {
        auto * p = std::get_if<Output::InputAddressed>(&output.output.raw);
        if (!p)
            return HashModulo::DeferredDrv{};
        convertedOutputs.insert({name, *p});
    }

    StringPairs env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);
    StructuredAttrs::checkKeyNotInUse(env);
    if (drv.structuredAttrs)
        env.insert(drv.structuredAttrs->unparse());

    auto maskedInputs = derivationModulo(store, FullInputs::fromSet(drv.inputs), drv.name);
    if (!maskedInputs)
        return HashModulo::DeferredDrv{};
    ATermT<FullInputs, Output::InputAddressed> masked{
        .outputs = std::move(convertedOutputs),
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = std::move(env),
    };
    return hashString(HashAlgorithm::SHA256, unparseDerivation(store, *maskedInputs, drv.name, masked));
}

/**
 * Build the ATerm-shaped intermediate whose outputs are replaced with
 * `Deferred` and whose env vars named after them are blanked, so the
 * hash does not depend on the derivation's own output paths. Only valid
 * for input-addressed (possibly deferred) derivations.
 */
template<typename Input>
static ATermT<FullInputs, Output::Deferred> maskOutputsAndEnv(const Derivation<Input, Output> & drv)
{
    StringPairs env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);

    Outputs<Output::Deferred> maskedOutputs;
    for (auto & [name, output] : drv.outputs) {
        std::visit(
            overloaded{
                [&](const Output::InputAddressed &) {},
                [&](const Output::Deferred &) {
                    /* Possibly pessimistically deferred --- we will fill in
                       the output paths. */
                },
                [&](const auto &) {
                    panic("hashModulo: unexpected output type, these derivation types are not input addressed");
                },
            },
            output.output.raw);
        maskedOutputs.insert({name, {}});
        if (auto j = env.find(name); j != env.end())
            j->second = "";
    }

    StructuredAttrs::checkKeyNotInUse(env);
    if (drv.structuredAttrs)
        env.insert(drv.structuredAttrs->unparse());

    return {
        .outputs = std::move(maskedOutputs),
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = std::move(env),
    };
}

/**
 * Compute the hash with outputs masked (replaced with `Deferred`).
 * Used by `processDerivationOutputPaths` to compute a derivation's own
 * output paths. Only valid for input-addressed (possibly deferred)
 * derivations.
 */
std::optional<Hash> hashModulo(Store & store, const Full & drv)
{
    auto maskedInputs = derivationModulo(store, FullInputs::fromSet(drv.inputs), drv.name);
    if (!maskedInputs)
        return std::nullopt;
    return hashString(HashAlgorithm::SHA256, unparseDerivation(store, *maskedInputs, drv.name, maskOutputsAndEnv(drv)));
}

Hash hashModulo(Store & store, const Basic & drv)
{
    /* A resolved derivation has no input derivations, so there is
       nothing to substitute and the hash is always computable. */
    HashModuloInputs maskedInputs{
        .srcs = drv.inputs,
        .drvs = {},
    };
    return hashString(HashAlgorithm::SHA256, unparseDerivation(store, maskedInputs, drv.name, maskOutputsAndEnv(drv)));
}

/* --------------------------------------------------------------------------
   Wire protocol serialisation
   -------------------------------------------------------------------------- */

static Output readOutput(Source & in, const StoreDirConfig & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseOutput(store, pathS, hashAlgo, hash, experimentalFeatureSettings);
}

Source & read(Source & in, const StoreDirConfig & store, BasicATerm & drv, std::string_view name)
{
    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto outputName = readString(in);
        auto output = readOutput(in, store);
        drv.outputs.emplace(std::move(outputName), std::move(output));
    }

    drv.inputs = CommonProto::Serialise<StorePathSet>::read(store, CommonProto::ReadConn{.from = in});
    in >> drv.platform >> drv.builder;
    drv.args = readStrings<Strings>(in);

    drv.env.clear();
    nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto key = readString(in);
        auto value = readString(in);
        drv.env.insert_or_assign(std::move(key), std::move(value));
    }

    return in;
}

void write(Sink & out, const StoreDirConfig & store, const BasicATerm & drv, std::string_view name)
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
                    out << store.printStorePath(dof.path(store, name, i.first)) << dof.ca.printMethodAlgo()
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

    out << drv.env.size();
    for (auto & [k, v] : drv.env)
        out << k << v;
}

} // namespace derivation

} // namespace nix
