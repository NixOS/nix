#include "nix/store/derivations.hh"
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
#include <optional>

namespace nix {

using namespace std::literals::string_view_literals;

std::optional<StorePath>
DerivationOutput::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return std::visit(
        overloaded{
            [](const DerivationOutput::InputAddressed & doi) -> std::optional<StorePath> { return {doi.path}; },
            [&](const DerivationOutput::CAFixed & dof) -> std::optional<StorePath> {
                return {dof.path(store, drvName, outputName)};
            },
            [](const DerivationOutput::CAFloating & dof) -> std::optional<StorePath> { return std::nullopt; },
            [](const DerivationOutput::Deferred &) -> std::optional<StorePath> { return std::nullopt; },
            [](const DerivationOutput::Impure &) -> std::optional<StorePath> { return std::nullopt; },
        },
        raw);
}

StorePath
DerivationOutput::CAFixed::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return store.makeFixedOutputPathFromCA(
        outputPathName(drvName, outputName), ContentAddressWithReferences::withoutRefs(ca));
}

bool DerivationType::isCA() const
{
    /* Normally we do the full `std::visit` to make sure we have
       exhaustively handled all variants, but so long as there is a
       variant called `ContentAddressed`, it must be the only one for
       which `isCA` is true for this to make sense!. */
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return true; },
            [](const Impure &) { return true; },
        },
        raw);
}

bool DerivationType::isFixed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::hasKnownOutputPaths() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return !ia.deferred; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::isSandboxed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return true; },
            [](const ContentAddressed & ca) { return ca.sandboxed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::isImpure() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return false; },
            [](const Impure &) { return true; },
        },
        raw);
}

bool isBuiltin(const BasicDerivation & drv)
{
    return drv.builder.substr(0, 8) == "builtin:";
}

template<typename Inputs, typename Output>
bool DerivationT<Inputs, Output>::isBuiltin() const
    requires std::is_same_v<Output, DerivationOutput>
{
    return builder.substr(0, 8) == "builtin:";
}

// Forward declaration of specialization
template<>
std::string DerivationT<FullInputs>::unparse(const StoreDirConfig & store) const;

static auto infoForDerivation(const StoreDirConfig & store, const Derivation & drv)
{
    auto references = drv.inputs.srcs;
    for (auto & i : drv.inputs.drvs.map)
        references.insert(i.first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = drv.unparse(store);
    auto hash = hashString(HashAlgorithm::SHA256, contents);
    auto ca = TextInfo{.hash = hash, .references = references};
    return std::tuple{
        suffix,
        contents,
        references,
        store.makeFixedOutputPathFromCA(suffix, ca),
    };
}

StorePath computeStorePath(const StoreDirConfig & store, const Derivation & drv)
{
    auto [_suffix, _contents, _references, path] = infoForDerivation(store, drv);
    return path;
}

StorePath Store::writeDerivation(const Derivation & drv, RepairFlag repair)
{
    auto [suffix, contents, references, path] = infoForDerivation(*this, drv);

    /* In case the derivation is already valid, we bail out early since that's
       faster. But we need to make sure that the derivation has a corresponding
       temproot. It is added by the remote in addToStoreFromDump, but we'd like
       to avoid sending a lot of drv contents to the daemon. */
    addTempRoot(path);

    if (isValidPath(path) && !repair)
        return path;

    StringSource s{contents};
    auto path2 = addToStoreFromDump(
        s,
        suffix,
        FileSerialisationMethod::Flat,
        ContentAddressMethod::Raw::Text,
        HashAlgorithm::SHA256,
        references,
        repair);
    assert(path2 == path);

    return path;
}

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
    Derivation drv;
    drv.name = name;

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
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto drvPath = parsePath(str);
        expect(str, ',');
        drv.inputs.drvs.map.insert_or_assign(
            store.parseStorePath(*drvPath), parseDerivedPathMapNode(store, str, version));
        expect(str, ')');
    }

    expect(str, ',');
    drv.inputs.srcs = store.parseStorePathSet(parseStrings(str, true));
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
template<typename Iter>
static bool hasDynamicDrvDep(Iter begin, Iter end)
{
    return std::find_if(begin, end, [](auto & kv) { return !kv.second.childMap.empty(); }) != end;
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
 * The one, unlike the public one, is polymorphic on the output parameter to
 * support the (private) `hashInputDerivationModulo`.
 */
template<typename Inputs, typename Output>
static std::string unparseDerivation(const StoreDirConfig & store, const DerivationT<Inputs, Output> & drv)
{
    // Hash derivation is only for input put addressing, with masking or unmasking cases
    if constexpr (std::is_same_v<Inputs, FullInputs>) {
        static_assert(std::is_same_v<Output, DerivationOutput>);
    } else if constexpr (std::is_same_v<Inputs, HashModuloInputs>) {
        static_assert(
            std::is_same_v<Output, DerivationOutput::InputAddressed>
            || std::is_same_v<Output, DerivationOutput::Deferred>);
    } else {
        static_assert(false);
    }

    std::string s;
    s.reserve(65536);

    /* Use older unversioned form if possible, for wider compat. Use
       newer form only if we need it, which we do for
       `Xp::DynamicDerivations`. */
    if (hasDynamicDrvDep(drv.inputs.drvs.map.begin(), drv.inputs.drvs.map.end())) {
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
std::string DerivationT<FullInputs>::unparse(const StoreDirConfig & store) const
{
    return unparseDerivation(store, *this);
}

// FIXME: remove
bool isDerivation(std::string_view fileName)
{
    return hasSuffix(fileName, drvExtension);
}

std::string outputPathName(std::string_view drvName, OutputNameView outputName)
{
    std::string res{drvName};
    if (outputName != "out"sv) {
        res += '-';
        res += outputName;
    }
    return res;
}

template<typename Inputs, typename Output>
DerivationType DerivationT<Inputs, Output>::type() const
    requires std::is_same_v<Output, DerivationOutput>
{
    std::optional<HashAlgorithm> floatingHashAlgo;
    std::optional<DerivationType> ty;

    auto decide = [&](DerivationType newTy) {
        if (!ty)
            ty = newTy;
        else if (ty.value() != newTy)
            throw Error("can't mix derivation output types");
        else if (ty.value() == DerivationType::ContentAddressed{.sandboxed = false, .fixed = true})
            // FIXME: Experimental feature?
            throw Error("only one fixed output is allowed for now");
    };

    for (auto & i : outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed &) {
                    decide(
                        DerivationType::InputAddressed{
                            .deferred = false,
                        });
                },
                [&](const DerivationOutput::CAFixed &) {
                    decide(
                        DerivationType::ContentAddressed{
                            .sandboxed = false,
                            .fixed = true,
                        });
                    if (i.first != "out"sv)
                        throw Error("single fixed output must be named \"out\"");
                },
                [&](const DerivationOutput::CAFloating & dof) {
                    decide(
                        DerivationType::ContentAddressed{
                            .sandboxed = true,
                            .fixed = false,
                        });
                    if (!floatingHashAlgo)
                        floatingHashAlgo = dof.hashAlgo;
                    else if (*floatingHashAlgo != dof.hashAlgo)
                        throw Error("all floating outputs must use the same hash algorithm");
                },
                [&](const DerivationOutput::Deferred &) {
                    decide(
                        DerivationType::InputAddressed{
                            .deferred = true,
                        });
                },
                [&](const DerivationOutput::Impure &) { decide(DerivationType::Impure{}); },
            },
            i.second.raw);
    }

    if (!ty)
        throw Error("must have at least one output");

    return ty.value();
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
static std::optional<Hash> hashDerivationModulo(Store & store, const Derivation & drv)
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
                        "hashDerivationModulo: unexpected output type, these derivaitons types are not input addressed");
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
            .inputs = drv.inputs,
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
            .inputs = drv.inputs,
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

static DerivationOutput readDerivationOutput(Source & in, const StoreDirConfig & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseDerivationOutput(store, pathS, hashAlgo, hash, experimentalFeatureSettings);
}

template<typename Inputs, typename Output>
StringSet DerivationT<Inputs, Output>::outputNames() const
    requires std::is_same_v<Output, DerivationOutput>
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}

template<typename Inputs, typename Output>
DerivationOutputsAndOptPaths DerivationT<Inputs, Output>::outputsAndOptPaths(const StoreDirConfig & store) const
    requires std::is_same_v<Output, DerivationOutput>
{
    DerivationOutputsAndOptPaths outsAndOptPaths;
    for (auto & [outputName, output] : outputs)
        outsAndOptPaths.insert(
            std::make_pair(outputName, std::make_pair(output, output.path(store, name, outputName))));
    return outsAndOptPaths;
}

template<typename Inputs, typename Output>
std::string_view DerivationT<Inputs, Output>::nameFromPath(const StorePath & drvPath)
{
    drvPath.requireDerivation();
    auto nameWithSuffix = drvPath.name();
    nameWithSuffix.remove_suffix(drvExtension.size());
    return nameWithSuffix;
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

std::string hashPlaceholder(const OutputNameView outputName)
{
    // FIXME: memoize?
    return "/"
           + hashString(HashAlgorithm::SHA256, concatStrings("nix-output:", outputName))
                 .to_string(HashFormat::Nix32, false);
}

template<typename Inputs, typename Output>
void DerivationT<Inputs, Output>::applyRewrites(const StringMap & rewrites)
    requires std::is_same_v<Output, DerivationOutput>
{
    if (rewrites.empty())
        return;

    debug("rewriting the derivation");

    for (auto & rewrite : rewrites)
        debug("rewriting %s as %s", rewrite.first, rewrite.second);

    builder = rewriteStrings(builder, rewrites);
    for (auto & arg : args)
        arg = rewriteStrings(arg, rewrites);

    StringPairs newEnv;
    for (auto & envVar : env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second, rewrites);
        newEnv.emplace(envName, envValue);
    }
    env = std::move(newEnv);

    if (structuredAttrs) {
        // TODO rewrite the JSON AST properly, rather than dump parse round trip.
        auto [_, jsonS] = structuredAttrs->unparse();
        jsonS = rewriteStrings(std::move(jsonS), rewrites);
        structuredAttrs = StructuredAttrs::parse(jsonS);
    }
}

template<>
Derivation DerivationT<StorePathSet>::unresolve() const
{
    return Derivation{
        .outputs = outputs,
        .inputs = {.srcs = inputs, .drvs = {}},
        .platform = platform,
        .builder = builder,
        .args = args,
        .env = env,
        .structuredAttrs = structuredAttrs,
        .name = name,
    };
}

template<>
bool Derivation::shouldResolve() const
{
    /* No input drvs means nothing to resolve. */
    if (inputs.drvs.map.empty())
        return false;

    auto drvType = type();

    bool typeNeedsResolve = std::visit(
        overloaded{
            [&](const DerivationType::InputAddressed & ia) {
                /* Must resolve if deferred. */
                return ia.deferred;
            },
            [&](const DerivationType::ContentAddressed & ca) {
                return ca.fixed
                           /* Can optionally resolve if fixed, which is good
                              for avoiding unnecessary rebuilds. */
                           ? experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
                           /* Must resolve if floating. */
                           : true;
            },
            [&](const DerivationType::Impure &) { return true; },
        },
        drvType.raw);

    /* Also need to resolve if any inputs are outputs of dynamic derivations. */
    bool hasDynamicInputs = std::ranges::any_of(
        inputs.drvs.map.begin(), inputs.drvs.map.end(), [](auto & pair) { return !pair.second.childMap.empty(); });

    return typeNeedsResolve || hasDynamicInputs;
}

template<bool fillIn>
static void processDerivationOutputPaths(Store & store, auto && drv, std::string_view drvName);

static bool tryResolveInput(
    const StoreDirConfig & store,
    StorePathSet & inputSrcs,
    StringMap & inputRewrites,
    const DownstreamPlaceholder * placeholderOpt,
    ref<const SingleDerivedPath> drvPath,
    const DerivedPathMap<StringSet>::ChildNode & inputNode,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain)
{
    auto getPlaceholder = [&](const std::string & outputName) {
        return placeholderOpt ? DownstreamPlaceholder::unknownDerivation(*placeholderOpt, outputName) : [&] {
            auto * p = std::get_if<SingleDerivedPath::Opaque>(&drvPath->raw());
            // otherwise we should have had a placeholder to build-upon already
            assert(p);
            return DownstreamPlaceholder::unknownCaOutput(p->path, outputName);
        }();
    };

    for (auto & outputName : inputNode.value) {
        auto actualPathOpt = queryResolutionChain(drvPath, outputName);
        if (!actualPathOpt)
            return false;
        auto actualPath = *actualPathOpt;
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
            inputRewrites.emplace(getPlaceholder(outputName).render(), store.printStorePath(actualPath));
        }
        inputSrcs.insert(std::move(actualPath));
    }

    for (auto & [outputName, childNode] : inputNode.childMap) {
        auto nextPlaceholder = getPlaceholder(outputName);
        if (!tryResolveInput(
                store,
                inputSrcs,
                inputRewrites,
                &nextPlaceholder,
                make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{drvPath, outputName}),
                childNode,
                queryResolutionChain))
            return false;
    }
    return true;
}

// Forward declaration of specialization
template<>
std::optional<BasicDerivation> DerivationT<FullInputs>::tryResolve(
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const;

template<>
std::optional<BasicDerivation> DerivationT<FullInputs>::tryResolve(Store & store, Store * evalStore) const
{
    return tryResolve(
        store, [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
            try {
                return resolveDerivedPath(store, SingleDerivedPath::Built{drvPath, outputName}, evalStore);
            } catch (Error &) {
                return std::nullopt;
            }
        });
}

template<>
std::optional<BasicDerivation> DerivationT<FullInputs>::tryResolve(
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const
{
    BasicDerivation resolved{
        .outputs = outputs,
        .inputs = inputs.srcs,
        .platform = platform,
        .builder = builder,
        .args = args,
        .env = env,
        .structuredAttrs = structuredAttrs,
        .name = name,
    };

    StringMap inputRewrites;

    for (auto & [inputDrv, inputNode] : inputs.drvs.map)
        if (!tryResolveInput(
                store,
                resolved.inputs,
                inputRewrites,
                nullptr,
                make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{inputDrv}),
                inputNode,
                queryResolutionChain))
            return std::nullopt;

    resolved.applyRewrites(inputRewrites);

    processDerivationOutputPaths</*maskOuputs=*/true>(store, resolved, resolved.name);

    return resolved;
}

/**
 * Process `InputAddressed`, `Deferred`, and `CAFixed` outputs.
 *
 * For `InputAddressed` outputs or `Deferred` outputs:
 *
 * - with `Regular` hash kind, validate `InputAddressed` outputs have
 *   the correct path (throws if mismatch). For `Deferred` outputs:
 *   - if `fillIn` is true, fill in the output path to make `InputAddressed`
 *   - if `fillIn` is false, throw an error
 *   Then validate or fill in the environment variable with the path.
 *
 * - with `Deferred` hash kind, validate that the output is either
 *   `InputAddressed` (error) or `Deferred` (correct).
 *
 * For `CAFixed` outputs, validate or fill in the environment variable
 * with the computed path.
 *
 * @tparam fillIn If true, fill in missing output paths and environment
 * variables. If false, validate that all paths are correct (throws on
 * mismatch).
 */
template<bool fillIn>
static void processDerivationOutputPaths(Store & store, auto && drv, std::string_view drvName)
{
    /* output optional is for whether we set it yet. Inner optional is
       for whether the input-addressed derivation has an input address
       now or is deferred --- can only calculate input address later. */
    std::optional<std::optional<Hash>> hashModulo_;

    auto hashModulo = [&]() -> const std::optional<Hash> & {
        if (!hashModulo_) {
            // somewhat expensive so we do lazily
            // Note that we do *not* recur with `fillIn`
            if constexpr (std::is_same_v<std::decay_t<decltype(drv)>, Derivation>) {
                hashModulo_ = hashDerivationModulo(store, drv);
            } else {
                hashModulo_ = hashDerivationModulo(store, drv.unresolve());
            }
        }
        return *hashModulo_;
    };

    for (auto & [outputName, output] : drv.outputs) {
        auto envHasRightPath = [&](const StorePath & actual, bool isDeferred = false) {
            if constexpr (fillIn) {
                auto j = drv.env.find(outputName);
                /* Fill in mode: fill in missing or empty environment
                   variables */
                if (j == drv.env.end())
                    drv.env.insert(j, {outputName, store.printStorePath(actual)});
                else if (j->second == "")
                    j->second = store.printStorePath(actual);
                /* We know validation will succeed after fill-in, but
                   just to be extra sure, validate unconditionally */
            }
            auto j = drv.env.find(outputName);
            if (j == drv.env.end())
                throw Error(
                    "derivation has missing environment variable '%s', should be '%s' but is not present",
                    outputName,
                    store.printStorePath(actual));
            if (j->second != store.printStorePath(actual)) {
                if (isDeferred)
                    warn(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'\nThis will be an error in future versions of Nix; compatibility of CA derivations will be broken.",
                        outputName,
                        store.printStorePath(actual),
                        j->second);
                else
                    throw Error(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'",
                        outputName,
                        store.printStorePath(actual),
                        j->second);
            }
        };
        auto hash = [&]<typename Output>(const Output & outputVariant) {
            auto & drvHash = hashModulo();
            if (drvHash) {
                auto outPath = store.makeOutputPath(outputName, *drvHash, drvName);

                if constexpr (std::is_same_v<Output, DerivationOutput::InputAddressed>) {
                    if (outputVariant.path == outPath) {
                        envHasRightPath(outPath);
                        return; // Correct case
                    }
                    /* Error case, an explicitly wrong path is
                       always an error. */
                    throw Error(
                        "derivation has incorrect output '%s', should be '%s'",
                        store.printStorePath(outputVariant.path),
                        store.printStorePath(outPath));
                } else if constexpr (std::is_same_v<Output, DerivationOutput::Deferred>) {
                    if constexpr (fillIn) {
                        /* Fill in output path for Deferred outputs */
                        output = DerivationOutput::InputAddressed{
                            .path = outPath,
                        };
                        envHasRightPath(outPath);
                    } else {
                        /* Validation mode: deferred outputs
                           should have been filled in */
                        warn(
                            "derivation has incorrect deferred output, should be '%s'.\nThis will be an error in future versions of Nix; compatibility of CA derivations will be broken.",
                            store.printStorePath(outPath));
                    }
                } else {
                    /* Will never happen, based on where
                       `hash` is called. */
                    static_assert(false);
                }
            } else {
                /* Deferred — hash not yet known. */
                if constexpr (std::is_same_v<Output, DerivationOutput::InputAddressed>) {
                    /* Error case, an explicitly wrong path is
                       always an error. */
                    throw Error(
                        "derivation has incorrect output '%s', should be deferred",
                        store.printStorePath(outputVariant.path));
                } else if constexpr (std::is_same_v<Output, DerivationOutput::Deferred>) {
                    /* Correct: Deferred output with Deferred hash kind. */
                } else {
                    /* Will never happen, based on where
                       `hash` is called. */
                    static_assert(false);
                }
            }
        };
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed & o) { hash(o); },
                [&](const DerivationOutput::Deferred & o) { hash(o); },
                [&](const DerivationOutput::CAFixed & dof) { envHasRightPath(dof.path(store, drvName, outputName)); },
                [&](const auto &) {
                    // Nothing to do for other output types
                },
            },
            output.raw);
    }

    /* Don't need the answer, but do this anyways to assert is proper
       combination. The code above is more general and naturally allows
       combinations that are currently prohibited. */
    drv.type();
}

template<typename Inputs, typename Output>
void DerivationT<Inputs, Output>::checkInvariants(Store & store, const StorePath & drvPath) const
    requires std::is_same_v<Output, DerivationOutput>
{
    assert(drvPath.isDerivation());
    std::string drvName(drvPath.name());
    drvName = drvName.substr(0, drvName.size() - drvExtension.size());

    if (drvName != name) {
        throw Error("derivation '%s' has name '%s' which does not match its path", store.printStorePath(drvPath), name);
    }

    try {
        checkInvariants(store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation '%s'", store.printStorePath(drvPath));
        throw;
    }
}

template<>
void BasicDerivation::checkInvariants(Store & store) const
{
    processDerivationOutputPaths<false>(store, *this, name);
}

template<>
void Derivation::checkInvariants(Store & store) const
{
    processDerivationOutputPaths<false>(store, *this, name);
}

template<>
void Derivation::fillInOutputPaths(Store & store)
{
    processDerivationOutputPaths<true>(store, *this, name);
}

template<>
Derivation Derivation::parseJsonAndValidate(Store & store, const nlohmann::json & json)
{
    auto drv = static_cast<Derivation>(json);

    drv.fillInOutputPaths(store);

    try {
        drv.checkInvariants(store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation from JSON with name '%s'", drv.name);
        throw;
    }

    return drv;
}

const Hash impureOutputHash = hashString(HashAlgorithm::SHA256, "impure");

// Explicit template instantiations
template struct DerivationT<StorePathSet>;
template struct DerivationT<FullInputs>;

} // namespace nix

namespace nlohmann {

using namespace nix;

void adl_serializer<DerivationOutput>::to_json(json & res, const DerivationOutput & o)
{
    res = nlohmann::json::object();
    std::visit(
        overloaded{
            [&](const DerivationOutput::InputAddressed & doi) { res["path"] = doi.path; },
            [&](const DerivationOutput::CAFixed & dof) {
                res = dof.ca;
        // FIXME print refs?
        /* it would be nice to output the path for user convenience, but
           this would require us to know the store dir. */
#if 0
                res["path"] = dof.path(store, drvName, outputName);
#endif
            },
            [&](const DerivationOutput::CAFloating & dof) {
                res["method"] = std::string{dof.method.render()};
                res["hashAlgo"] = printHashAlgo(dof.hashAlgo);
            },
            [&](const DerivationOutput::Deferred &) {},
            [&](const DerivationOutput::Impure & doi) {
                res["method"] = std::string{doi.method.render()};
                res["hashAlgo"] = printHashAlgo(doi.hashAlgo);
                res["impure"] = true;
            },
        },
        o.raw);
}

DerivationOutput
adl_serializer<DerivationOutput>::from_json(const json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    std::set<std::string_view> keys;
    auto & json = getObject(_json);

    for (const auto & [key, _] : json)
        keys.insert(key);

    auto methodAlgo = [&]() -> std::pair<ContentAddressMethod, HashAlgorithm> {
        ContentAddressMethod method = ContentAddressMethod::parse(getString(valueAt(json, "method")));
        if (method == ContentAddressMethod::Raw::Text)
            xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output in JSON");

        auto hashAlgo = parseHashAlgo(getString(valueAt(json, "hashAlgo")));
        return {std::move(method), std::move(hashAlgo)};
    };

    if (keys == (std::set<std::string_view>{"path"})) {
        return DerivationOutput::InputAddressed{
            .path = valueAt(json, "path"),
        };
    }

    else if (keys == (std::set<std::string_view>{"method", "hash"})) {
        auto dof = DerivationOutput::CAFixed{
            .ca = static_cast<ContentAddress>(_json),
        };
        if (dof.ca.method == ContentAddressMethod::Raw::Text)
            xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output in JSON");
        /* We no longer produce this (denormalized) field (for the
           reasons described above), so we don't need to check it. */
#if 0
        if (dof.path(store, drvName, outputName) != static_cast<StorePath>(valueAt(json, "path")))
            throw Error("Path doesn't match derivation output");
#endif
        return dof;
    }

    else if (keys == (std::set<std::string_view>{"method", "hashAlgo"})) {
        xpSettings.require(Xp::CaDerivations);
        auto [method, hashAlgo] = methodAlgo();
        return DerivationOutput::CAFloating{
            .method = std::move(method),
            .hashAlgo = std::move(hashAlgo),
        };
    }

    else if (keys == (std::set<std::string_view>{})) {
        return DerivationOutput::Deferred{};
    }

    else if (keys == (std::set<std::string_view>{"method", "hashAlgo", "impure"})) {
        xpSettings.require(Xp::ImpureDerivations);
        auto [method, hashAlgo] = methodAlgo();
        return DerivationOutput::Impure{
            .method = std::move(method),
            .hashAlgo = hashAlgo,
        };
    }

    else {
        throw Error("invalid JSON for derivation output");
    }
}

static void inputsToJson(json & res, const StorePathSet & inputs)
{
    res = nlohmann::json::array();
    for (auto & input : inputs)
        res.emplace_back(input);
}

static void inputsToJson(json & res, const FullInputs & inputs)
{
    res = nlohmann::json::object();

    inputsToJson(res["srcs"], inputs.srcs);

    auto doInput = [&](this const auto & doInput, const auto & inputNode) -> nlohmann::json {
        auto value = nlohmann::json::object();
        value["outputs"] = inputNode.value;
        {
            auto next = nlohmann::json::object();
            for (auto & [outputId, childNode] : inputNode.childMap)
                next[outputId] = doInput(childNode);
            value["dynamicOutputs"] = std::move(next);
        }
        return value;
    };

    auto & inputDrvsObj = res["drvs"];
    inputDrvsObj = nlohmann::json::object();
    for (auto & [inputDrv, inputNode] : inputs.drvs.map)
        inputDrvsObj[inputDrv.to_string()] = doInput(inputNode);
}

template<typename Inputs>
void adl_serializer<DerivationT<Inputs>>::to_json(json & res, const DerivationT<Inputs> & d)
{
    res = nlohmann::json::object();

    res["name"] = d.name;
    res["version"] = expectedJsonVersionDerivation;

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : d.outputs)
            outputsObj[outputName] = output;
    }

    inputsToJson(res["inputs"], d.inputs);

    res["system"] = d.platform;
    res["builder"] = d.builder;
    res["args"] = d.args;
    res["env"] = d.env;

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

template<typename Inputs>
static Inputs inputsFromJson(const json & inputsJson, const ExperimentalFeatureSettings & xpSettings);

template<>
StorePathSet inputsFromJson<StorePathSet>(const json & inputsJson, const ExperimentalFeatureSettings &)
{
    StorePathSet inputSrcs;
    for (auto & input : getArray(inputsJson))
        inputSrcs.insert(input);
    return inputSrcs;
}

template<>
FullInputs inputsFromJson<FullInputs>(const json & inputsJson, const ExperimentalFeatureSettings & xpSettings)
{
    auto inputsObj = getObject(inputsJson);
    FullInputs inputs;

    try {
        for (auto & input : getArray(valueAt(inputsObj, "srcs")))
            inputs.srcs.insert(input);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'srcs'");
        throw;
    }

    try {
        auto doInput = [&](this const auto & doInput, const auto & _json) -> DerivedPathMap<StringSet>::ChildNode {
            auto & json = getObject(_json);
            DerivedPathMap<StringSet>::ChildNode node;
            node.value = getStringSet(valueAt(json, "outputs"));
            for (auto & [outputId, childNode] : getObject(valueAt(json, "dynamicOutputs"))) {
                xpSettings.require(
                    Xp::DynamicDerivations, [&] { return fmt("dynamic output '%s' in JSON", outputId); });
                node.childMap[outputId] = doInput(childNode);
            }
            return node;
        };
        for (auto & [inputDrvPath, inputOutputs] : getObject(valueAt(inputsObj, "drvs")))
            inputs.drvs.map[StorePath{inputDrvPath}] = doInput(inputOutputs);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'drvs'");
        throw;
    }

    return inputs;
}

template<typename Inputs>
DerivationT<Inputs>
adl_serializer<DerivationT<Inputs>>::from_json(const json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    auto & json = getObject(_json);
    {
        auto version = getUnsigned(valueAt(json, "version"));
        if (version != expectedJsonVersionDerivation)
            throw Error(
                "Unsupported derivation JSON format version %d, only format version %d is currently supported.",
                version,
                expectedJsonVersionDerivation);
    }

    return DerivationT<Inputs>{
        .outputs =
            [&] {
                DerivationOutputs<> outputs;
                try {
                    for (auto & [outputName, output] : getObject(valueAt(json, "outputs")))
                        outputs.insert_or_assign(
                            outputName, adl_serializer<DerivationOutput>::from_json(output, xpSettings));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'outputs'");
                    throw;
                }
                return outputs;
            }(),
        .inputs =
            [&] {
                try {
                    return inputsFromJson<Inputs>(valueAt(json, "inputs"), xpSettings);
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'inputs'");
                    throw;
                }
            }(),
        .platform = getString(valueAt(json, "system")),
        .builder = getString(valueAt(json, "builder")),
        .args = getStringList(valueAt(json, "args")),
        .env =
            [&] {
                try {
                    return getStringMap(valueAt(json, "env"));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'env'");
                    throw;
                }
            }(),
        .structuredAttrs = [&]() -> std::optional<StructuredAttrs> {
            if (auto structuredAttrs = get(json, "structuredAttrs"))
                return StructuredAttrs{*structuredAttrs};
            return std::nullopt;
        }(),
        .name = getString(valueAt(json, "name")),
    };
}

template struct adl_serializer<BasicDerivation>;
template struct adl_serializer<Derivation>;

} // namespace nlohmann
