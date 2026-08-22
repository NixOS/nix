#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/modulo.hh"
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

/* Store paths in derivations must be written in their canonical form. */
static StorePath parseStorePath(const StoreDirConfig & store, StringViewStream & str)
{
    return store.parseStorePathCanonical(*parseString(str));
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

static StringSet parseStrings(StringViewStream & str)
{
    StringSet res;
    expect(str, '[');
    while (!endOfList(str))
        res.insert(parseString(str).toOwned());
    return res;
}

static StorePathSet parseStorePaths(const StoreDirConfig & store, StringViewStream & str)
{
    StorePathSet res;
    expect(str, '[');
    while (!endOfList(str))
        res.insert(parseStorePath(store, str));
    return res;
}

static Output parseOutput(
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
            [[maybe_unused]] auto path = store.parseStorePathCanonical(pathS);
            auto hash = Hash::parseNonSRIUnprefixed(hashS, hashAlgo);
            Output::CAFixed dof{
                .ca =
                    ContentAddress{
                        .method = std::move(method),
                        .hash = std::move(hash),
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
                    "derivation output '%s' has path '%s', which does not match its content address",
                    outputName,
                    pathS);
#endif
            return dof;
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
        return Output::InputAddressed{
            .path = store.parseStorePathCanonical(pathS),
        };
    }
}

static Output parseOutput(
    const StoreDirConfig & store,
    std::string_view drvName,
    OutputNameView outputName,
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

    return parseOutput(store, drvName, outputName, *pathS, *hashAlgo, *hash, xpSettings);
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

Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    using namespace std::literals::string_view_literals;

    Full drv{
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
        auto output = parseOutput(store, name, id, str, xpSettings);
        drv.outputs.emplace(std::move(id), std::move(output));
    }

    /* Parse the list of input derivations. */
    derivation::FullInputs fullInputs;
    expect(str, ",["sv);
    while (!endOfList(str)) {
        expect(str, '(');
        auto drvPath = parseStorePath(store, str);
        expect(str, ',');
        auto node = parseDerivedPathMapNode(store, str, version);
        /* Such an entry cannot be represented in the flat inputs set,
           and would thus be silently dropped rather than round-tripped.
           Nix itself never produces one. */
        if (node.value.empty() && node.childMap.empty())
            throw FormatError("inputDrvs entry for '%s' specifies no outputs", store.printStorePath(drvPath));
        fullInputs.drvs.map.insert_or_assign(std::move(drvPath), std::move(node));
        expect(str, ')');
    }

    expect(str, ',');
    fullInputs.srcs = parseStorePaths(store, str);
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
    /* Store paths must go through the escaping writer. On Unix they contain no
       character `printString` would escape, so this is byte-identical there; on
       Windows they contain backslashes, which the unquoted writer emits raw and
       the reader then mis-decodes (`\n` in `...\nix\store` becomes a newline). */
    printString(s, store.printStorePath(doi.path));
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
    /* See the note at the `doi.path` write above: store paths need escaping. */
    printString(s, store.printStorePath(dof.path(store, drvName, outputName)));
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
 * support the hash modulo intermediate form.
 */
template<typename Inputs, typename Out>
std::string unparse(const Derivation<Inputs, Out> & drv, const StoreDirConfig & store)
    requires(
        // Regular `FullInputs` case must have regular `Output` outputs
        (std::is_same_v<Inputs, FullInputs> && std::is_same_v<Out, Output>)
        // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
        || (std::is_same_v<Inputs, modulo::HashInputs>
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
        dynDrvDep = hasDynamicDrvDep(drv.inputs);
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
        /* `keyToString` renders a store path for the `StorePath` overload. */
        printString(s, keyToString(store, key));
        unparseDerivedPathMapNode(store, s, node);
        s += ')';
    }

    s += "],"sv;
    auto paths = store.printStorePathSet(drv.inputs.srcs); // FIXME: slow
    /* `paths` are store paths; see the note at the `doi.path` write above. */
    printStrings(s, paths.begin(), paths.end());

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

/* The hash modulo intermediate forms, unparsed by `modulo.cc`. */
template std::string
unparse(const Derivation<modulo::HashInputs, Output::Deferred> & drv, const StoreDirConfig & store);
template std::string
unparse(const Derivation<modulo::HashInputs, Output::InputAddressed> & drv, const StoreDirConfig & store);

std::string unparse(const Full & drv, const StoreDirConfig & store)
{
    // Convert to FullInputs for ATerm serialization
    return unparse(
        drv.mapInputs([](const std::set<SingleDerivedPath> & inputs) { return FullInputs::fromSet(inputs); }), store);
}

/* --------------------------------------------------------------------------
   Wire protocol serialisation
   -------------------------------------------------------------------------- */

static Output readOutput(Source & in, const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseOutput(store, drvName, outputName, pathS, hashAlgo, hash, experimentalFeatureSettings);
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
