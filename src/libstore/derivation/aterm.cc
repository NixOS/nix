#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/util/strings-inline.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

#include <algorithm>
#include <optional>
#include <ranges>

namespace nix {

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

static DerivationOutput parseDerivationOutput(
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
            return DerivationOutput::Impure{
                .method = std::move(method),
                .hashAlgo = std::move(hashAlgo),
            };
        } else if (!hashS.empty()) {
            validatePath(pathS);
            auto hash = Hash::parseNonSRIUnprefixed(hashS, hashAlgo);
            return DerivationOutput::CAFixed{
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
            return DerivationOutput::CAFloating{
                .method = std::move(method),
                .hashAlgo = std::move(hashAlgo),
            };
        }
    } else {
        if (pathS.empty()) {
            return DerivationOutput::Deferred{};
        }
        validatePath(pathS);
        return DerivationOutput::InputAddressed{
            .path = store.parseStorePath(pathS),
        };
    }
}

static DerivationOutput parseDerivationOutput(
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

    return parseDerivationOutput(store, *pathS, *hashAlgo, *hash, xpSettings);
}

/**
 * All ATerm Derivation format versions currently known.
 *
 * Unknown versions are rejected at the parsing stage.
 */
enum struct DerivationATermVersion {
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
parseDerivedPathMapNode(const StoreDirConfig & store, StringViewStream & str, DerivationATermVersion version)
{
    using namespace std::literals::string_view_literals;

    DerivedPathMap<StringSet>::ChildNode node;

    auto parseNonDynamic = [&]() { node.value = parseStrings(str, false); };

    // Older derivation should never use new form, but newer
    // derivaiton can use old form.
    switch (version) {
    case DerivationATermVersion::Traditional:
        parseNonDynamic();
        break;
    case DerivationATermVersion::DynamicDerivations:
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

Derivation parseDerivation(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    Derivation drv{
        .name = std::string{name},
    };

    StringViewStream str{s};
    expect(str, 'D');
    DerivationATermVersion version;
    switch (str.peek()) {
    case 'e':
        expect(str, "erive("sv);
        version = DerivationATermVersion::Traditional;
        break;
    case 'r': {
        expect(str, "rvWithVersion("sv);
        auto versionS = parseString(str);
        if (*versionS == "xp-dyn-drv"sv) {
            // Only version we have so far
            version = DerivationATermVersion::DynamicDerivations;
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
        auto output = parseDerivationOutput(store, str, xpSettings);
        drv.outputs.emplace(std::move(id), std::move(output));
    }

    /* Parse the list of input derivations. */
    FullInputs fullInputs;
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto drvPath = parsePath(str);
        expect(str, ',');
        fullInputs.drvs.map.insert_or_assign(
            store.parseStorePath(*drvPath), parseDerivedPathMapNode(store, str, version));
        expect(str, ')');
    }

    expect(str, ',');
    fullInputs.srcs = store.parseStorePathSet(parseStrings(str, true));
    drv.inputs = fullInputs.toSet();
    expect(str, ',');
    drv.platform = parseString(str).toOwned();
    expect(str, ',');
    drv.builder = parseString(str).toOwned();

    /* Parse the builder arguments. */
    expect(str, ",["sv);
    while (!endOfList(str))
        drv.args.push_back(parseString(str).toOwned());

    /* Parse the environment variables. */
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto name = parseString(str).toOwned();
        expect(str, ',');
        auto value = parseString(str);
        if (name == StructuredAttrs::envVarName) {
            drv.structuredAttrs = StructuredAttrs::parse(*std::move(value));
        } else {
            drv.env.insert_or_assign(std::move(name), std::move(value).toOwned());
        }
        expect(str, ')');
    }

    expect(str, ')');
    return drv;
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

    using DrvMap = std::map<Hash, DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode>;

    /**
     * Nesting just to match `DerivedPathMap` for easier templating.
     */
    struct
    {
        DrvMap map;
    } drvs;

    // no operator== needed; this type is internal-only
};

/**
 * Does the derivation have a dependency on the output of a dynamic
 * derivation?
 *
 * In other words, does it on the output of derivation that is itself an
 * output of a derivation? This corresponds to a dependency that is an
 * inductive derived path with more than one layer of
 * `DerivedPath::Built`.
 */
template<typename Inputs>
static bool hasDynamicDrvDep(const Inputs & inputs)
    // Both `FullInputs` and `HashModuloInputs` have this shape
    requires requires { inputs.drvs.map; }
{
    return std::ranges::any_of(inputs.drvs.map, [](auto & kv) { return !kv.second.childMap.empty(); });
}

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
    const DerivationOutput::InputAddressed & doi,
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
    const DerivationOutput::CAFixed & dof,
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
    const StoreDirConfig &,
    std::string & s,
    const DerivationOutput::CAFloating & dof,
    std::string_view,
    std::string_view)
{
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo));
    s += ',';
    printUnquotedString(s, {});
}

static void unparseOutput(
    const StoreDirConfig &, std::string & s, const DerivationOutput::Deferred &, std::string_view, std::string_view)
{
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
    s += ',';
    printUnquotedString(s, {});
}

static void unparseOutput(
    const StoreDirConfig &, std::string & s, const DerivationOutput::Impure & doi, std::string_view, std::string_view)
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
    const DerivationOutput & output,
    std::string_view drvName,
    std::string_view outputName)
{
    std::visit([&](const auto & o) { unparseOutput(store, s, o, drvName, outputName); }, output.raw);
}

/**
 * This one, unlike the public one, is polymorphic on the output parameter to
 * support the (private) `hashInputDerivationModulo`.
 */
template<typename Inputs, typename Output>
static std::string unparseDerivation(const StoreDirConfig & store, const DerivationT<Inputs, Output> & drv)
    requires(
        // Regular `FullInputs` case must have regular `DerivationOutput` outputs
        (std::is_same_v<Inputs, FullInputs> && std::is_same_v<Output, DerivationOutput>)
        // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
        || (std::is_same_v<Inputs, HashModuloInputs>
            && (std::is_same_v<Output, DerivationOutput::InputAddressed>
                || std::is_same_v<Output, DerivationOutput::Deferred>) ))
{
    using namespace std::literals::string_view_literals;

    std::string s;
    s.reserve(65536);

    /* Use older unversioned form if possible, for wider compat. Use
       newer form only if we need it, which we do for
       `Xp::DynamicDerivations`. */
    if (hasDynamicDrvDep(drv.inputs)) {
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
        unparseOutput(store, s, i.second, drv.name, i.first);
        s += ')';
    }

    s += "],["sv;
    first = true;
    for (auto & [key, node] : drv.inputs.drvs.map) {
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
    auto paths = store.printStorePathSet(drv.inputs.srcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ',';
    printUnquotedString(s, drv.platform);
    s += ',';
    printString(s, drv.builder);
    s += ',';
    printStrings(s, drv.args.begin(), drv.args.end());

    s += ",["sv;
    first = true;

    auto unparseEnv = [&](const StringPairs & atermEnv) {
        for (auto & i : atermEnv) {
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
    };

    StructuredAttrs::checkKeyNotInUse(drv.env);
    if (drv.structuredAttrs) {
        StringPairs scratch = drv.env;
        scratch.insert(drv.structuredAttrs->unparse());
        unparseEnv(scratch);
    } else {
        unparseEnv(drv.env);
    }

    s += "])"sv;

    return s;
}

template<>
std::string Derivation::unparse(const StoreDirConfig & store) const
{
    // Convert to FullInputs for ATerm serialization
    DerivationT<FullInputs> fullDrv{
        .outputs = outputs,
        .inputs = FullInputs::fromSet(inputs),
        .platform = platform,
        .builder = builder,
        .args = args,
        .env = env,
        .structuredAttrs = structuredAttrs,
        .name = name,
    };
    return unparseDerivation(store, fullDrv);
}

DrvHashes drvHashes;

/* pathDerivationModulo and hashInputDerivationModulo are mutually recursive
 */

/**
 * Look up the derivation by value and memoize the `hashInputDerivationModulo` call.
 */
static DrvHashModulo pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    std::optional<DrvHashModulo> hash;
    if (drvHashes.cvisit(drvPath, [&hash](const auto & kv) { hash.emplace(kv.second); })) {
        return *hash;
    }
    auto h = hashInputDerivationModulo(store, store.readInvalidDerivation(drvPath));

    // Cache it
    drvHashes.insert_or_assign(drvPath, h);
    return h;
}

/**
 * Look up the hash modulo for the input derivation at `drvPath` and
 * insert the result into `drvInputs`.
 *
 * Returns `true` if deferred and cannot mutate (the caller should bail out).
 */
static bool pathDerivationModulo(
    Store & store,
    HashModuloInputs::DrvMap & drvInputs,
    const StorePath & drvPath,
    const DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode & node,
    std::string_view drvName)
{
    /* Need to build and resolve dynamic derivations first */
    if (!node.childMap.empty())
        return true;

    const auto & res = pathDerivationModulo(store, drvPath);
    return std::visit(
        overloaded{
            [&](const DrvHashModulo::DeferredDrv &) { return true; },
            // Regular non-CA derivation, replace derivation
            [&](const DrvHashModulo::DrvHash & drvHash) {
                drvInputs.insert_or_assign(drvHash, node);
                return false;
            },
            // CA derivation's output hashes
            [&](const DrvHashModulo::CaOutputHashes & outputHashes) {
                for (auto & outputName : node.value) {
                    /* Put each one in with a single "out" output.. */
                    const auto h = get(outputHashes, outputName);
                    if (!h)
                        throw Error("no hash for output '%s' of derivation '%s'", outputName, drvName);
                    drvInputs.insert_or_assign(
                        *h,
                        DerivedPathMap<StringSet>::ChildNode{
                            .value = {"out"},
                        });
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
 * When `Output` is `DerivationOutput::Deferred`, outputs are masked:
 * output paths and matching env vars are blanked so the hash does not
 * depend on its own output paths.
 *
 * When `Output` is `DerivationOutput`, outputs are preserved as-is.
 *
 * Returns `std::nullopt` if any input is deferred (depends on a CA or
 * dynamic derivation whose outputs are not yet known).
 */
template<typename Output>
static std::optional<DerivationT<HashModuloInputs, Output>>
derivationModulo(Store & store, DerivationT<FullInputs, Output> drv)
{
    DerivationT<HashModuloInputs, Output> masked{
        .outputs = std::move(drv.outputs),
        .inputs =
            {
                .srcs = std::move(drv.inputs.srcs),
                .drvs = {},
            },
        .platform = std::move(drv.platform),
        .builder = std::move(drv.builder),
        .args = std::move(drv.args),
        .env = std::move(drv.env),
        .structuredAttrs = std::move(drv.structuredAttrs),
        .name = std::move(drv.name),
    };

    for (auto & [drvPath, node] : drv.inputs.drvs.map)
        if (pathDerivationModulo(store, masked.inputs.drvs.map, drvPath, node, drv.name))
            return std::nullopt;

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
 * Compute the hash with outputs masked (replaced with `Deferred`).
 * Used by `processDerivationOutputPaths` to compute a derivation's own
 * output paths. Only valid for input-addressed (possibly deferred)
 * derivations.
 */
std::optional<Hash> hashDerivationModulo(Store & store, const Derivation & drv)
{
    DerivationOutputs<DerivationOutput::Deferred> maskedOutputs;
    StringPairs env = drv.env;
    for (auto & [name, output] : drv.outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed &) {},
                [&](const DerivationOutput::Deferred &) {
                    /* Possibly pessimistically deferred — we will fill in
                       the output paths. */
                },
                [&](const auto &) {
                    panic(
                        "hashDerivationModulo: unexpected output type, these derivation types are not input addressed");
                },
            },
            output.raw);
        maskedOutputs.insert({name, {}});
        auto j = env.find(name);
        if (j != env.end())
            j->second = "";
    }

    auto masked = derivationModulo(
        store,
        DerivationT<FullInputs, DerivationOutput::Deferred>{
            .outputs = std::move(maskedOutputs),
            .inputs = FullInputs::fromSet(drv.inputs),
            .platform = drv.platform,
            .builder = drv.builder,
            .args = drv.args,
            .env = std::move(env),
            .structuredAttrs = drv.structuredAttrs,
            .name = drv.name,
        });
    if (!masked)
        return std::nullopt;
    return hashString(HashAlgorithm::SHA256, unparseDerivation(store, *masked));
}

/**
 * Compute the hash with outputs preserved (as `InputAddressed`).
 * Used for computing a derivation's identity as an input to other
 * derivations.
 *
 * Returns the appropriate `DrvHashModulo` variant:
 * - `CaOutputHashes` for fixed-output CA derivations
 * - `DeferredDrv` for deferred, non-fixed CA, or impure derivations
 * - `DrvHash` for regular input-addressed derivations
 */
DrvHashModulo hashInputDerivationModulo(Store & store, const Derivation & drv)
{
    /* Return a fixed hash for fixed-output derivations. */
    if (drv.type().isFixed()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<DerivationOutput::CAFixed>(i.second.raw);
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
    DerivationOutputs<DerivationOutput::InputAddressed> convertedOutputs;
    for (auto & [name, output] : drv.outputs) {
        auto * p = std::get_if<DerivationOutput::InputAddressed>(&output.raw);
        if (!p)
            return DrvHashModulo::DeferredDrv{};
        convertedOutputs.insert({name, *p});
    }

    auto masked = derivationModulo(
        store,
        DerivationT<FullInputs, DerivationOutput::InputAddressed>{
            .outputs = std::move(convertedOutputs),
            .inputs = FullInputs::fromSet(drv.inputs),
            .platform = drv.platform,
            .builder = drv.builder,
            .args = drv.args,
            .env = drv.env,
            .structuredAttrs = drv.structuredAttrs,
            .name = drv.name,
        });
    if (!masked)
        return DrvHashModulo::DeferredDrv{};
    return hashString(HashAlgorithm::SHA256, unparseDerivation(store, *masked));
}

/* Wire protocol serialization, which also uses the legacy ATerm-style
   representation. */

static DerivationOutput readDerivationOutput(Source & in, const StoreDirConfig & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseDerivationOutput(store, pathS, hashAlgo, hash, experimentalFeatureSettings);
}

Source & readDerivation(Source & in, const StoreDirConfig & store, BasicDerivation & drv, std::string_view name)
{
    drv.name = name;

    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto name = readString(in);
        auto output = readDerivationOutput(in, store);
        drv.outputs.emplace(std::move(name), std::move(output));
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

void writeDerivation(Sink & out, const StoreDirConfig & store, const BasicDerivation & drv)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs) {
        out << i.first;
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed & doi) {
                    out << store.printStorePath(doi.path) << ""
                        << "";
                },
                [&](const DerivationOutput::CAFixed & dof) {
                    out << store.printStorePath(dof.path(store, drv.name, i.first)) << dof.ca.printMethodAlgo()
                        << dof.ca.hash.to_string(HashFormat::Base16, false);
                },
                [&](const DerivationOutput::CAFloating & dof) {
                    out << "" << (std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo)) << "";
                },
                [&](const DerivationOutput::Deferred &) {
                    out << ""
                        << ""
                        << "";
                },
                [&](const DerivationOutput::Impure & doi) {
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

} // namespace nix
