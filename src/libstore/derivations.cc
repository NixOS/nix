#include "nix/store/derivations.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/json-utils.hh"

#include <boost/container/small_vector.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <nlohmann/json.hpp>

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

bool BasicDerivation::isBuiltin() const
{
    return builder.substr(0, 8) == "builtin:";
}

static auto infoForDerivation(Store & store, const Derivation & drv)
{
    auto references = drv.inputSrcs;
    for (auto & i : drv.inputDrvs.map)
        references.insert(i.first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = drv.unparse(store, false);
    auto hash = hashString(HashAlgorithm::SHA256, contents);
    auto ca = TextInfo{.hash = hash, .references = references};
    return std::tuple{
        suffix,
        contents,
        references,
        store.makeFixedOutputPathFromCA(suffix, ca),
    };
}

StorePath writeDerivation(Store & store, const Derivation & drv, RepairFlag repair, bool readOnly)
{
    if (readOnly || settings.readOnlyMode) {
        auto [_x, _y, _z, path] = infoForDerivation(store, drv);
        return path;
    } else
        return store.writeDerivation(drv, repair);
}

StorePath Store::writeDerivation(const Derivation & drv, RepairFlag repair)
{
    auto [suffix, contents, references, path] = infoForDerivation(*this, drv);

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
            break;
        }
        start = idx + 1;
    }

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
        drv.inputDrvs.map.insert_or_assign(
            store.parseStorePath(*drvPath), parseDerivedPathMapNode(store, str, version));
        expect(str, ')');
    }

    expect(str, ',');
    drv.inputSrcs = store.parseStorePathSet(parseStrings(str, true));
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
    res.reserve(res.size() + s.size() * 2 + 2);
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
 * Does the derivation have a dependency on the output of a dynamic
 * derivation?
 *
 * In other words, does it on the output of derivation that is itself an
 * output of a derivation? This corresponds to a dependency that is an
 * inductive derived path with more than one layer of
 * `DerivedPath::Built`.
 */
static bool hasDynamicDrvDep(const Derivation & drv)
{
    return std::find_if(
               drv.inputDrvs.map.begin(),
               drv.inputDrvs.map.end(),
               [](auto & kv) { return !kv.second.childMap.empty(); })
           != drv.inputDrvs.map.end();
}

std::string Derivation::unparse(
    const StoreDirConfig & store, bool maskOutputs, DerivedPathMap<StringSet>::ChildNode::Map * actualInputs) const
{
    std::string s;
    s.reserve(65536);

    /* Use older unversioned form if possible, for wider compat. Use
       newer form only if we need it, which we do for
       `Xp::DynamicDerivations`. */
    if (hasDynamicDrvDep(*this)) {
        s += "DrvWithVersion("sv;
        // Only version we have so far
        printUnquotedString(s, "xp-dyn-drv"sv);
        s += ',';
    } else {
        s += "Derive("sv;
    }

    bool first = true;
    s += '[';
    for (auto & i : outputs) {
        if (first)
            first = false;
        else
            s += ',';
        s += '(';
        printUnquotedString(s, i.first);
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed & doi) {
                    s += ',';
                    printUnquotedString(s, maskOutputs ? ""sv : store.printStorePath(doi.path));
                    s += ',';
                    printUnquotedString(s, {});
                    s += ',';
                    printUnquotedString(s, {});
                },
                [&](const DerivationOutput::CAFixed & dof) {
                    s += ',';
                    printUnquotedString(s, maskOutputs ? ""sv : store.printStorePath(dof.path(store, name, i.first)));
                    s += ',';
                    printUnquotedString(s, dof.ca.printMethodAlgo());
                    s += ',';
                    printUnquotedString(s, dof.ca.hash.to_string(HashFormat::Base16, false));
                },
                [&](const DerivationOutput::CAFloating & dof) {
                    s += ',';
                    printUnquotedString(s, {});
                    s += ',';
                    printUnquotedString(s, std::string{dof.method.renderPrefix()} + printHashAlgo(dof.hashAlgo));
                    s += ',';
                    printUnquotedString(s, {});
                },
                [&](const DerivationOutput::Deferred &) {
                    s += ',';
                    printUnquotedString(s, {});
                    s += ',';
                    printUnquotedString(s, {});
                    s += ',';
                    printUnquotedString(s, {});
                },
                [&](const DerivationOutput::Impure & doi) {
                    // FIXME
                    s += ',';
                    printUnquotedString(s, {});
                    s += ',';
                    printUnquotedString(s, std::string{doi.method.renderPrefix()} + printHashAlgo(doi.hashAlgo));
                    s += ',';
                    printUnquotedString(s, "impure"sv);
                }},
            i.second.raw);
        s += ')';
    }

    s += "],["sv;
    first = true;
    if (actualInputs) {
        for (auto & [drvHashModulo, childMap] : *actualInputs) {
            if (first)
                first = false;
            else
                s += ',';
            s += '(';
            printUnquotedString(s, drvHashModulo);
            unparseDerivedPathMapNode(store, s, childMap);
            s += ')';
        }
    } else {
        for (auto & [drvPath, childMap] : inputDrvs.map) {
            if (first)
                first = false;
            else
                s += ',';
            s += '(';
            printUnquotedString(s, store.printStorePath(drvPath));
            unparseDerivedPathMapNode(store, s, childMap);
            s += ')';
        }
    }

    s += "],"sv;
    auto paths = store.printStorePathSet(inputSrcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ',';
    printUnquotedString(s, platform);
    s += ',';
    printString(s, builder);
    s += ',';
    printStrings(s, args.begin(), args.end());

    s += ",["sv;
    first = true;

    auto unparseEnv = [&](const StringPairs atermEnv) {
        for (auto & i : atermEnv) {
            if (first)
                first = false;
            else
                s += ',';
            s += '(';
            printString(s, i.first);
            s += ',';
            printString(s, maskOutputs && outputs.count(i.first) ? ""sv : i.second);
            s += ')';
        }
    };

    StructuredAttrs::checkKeyNotInUse(env);
    if (structuredAttrs) {
        StringPairs scratch = env;
        scratch.insert(structuredAttrs->unparse());
        unparseEnv(scratch);
    } else {
        unparseEnv(env);
    }

    s += "])"sv;

    return s;
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

DerivationType BasicDerivation::type() const
{
    std::set<std::string_view> inputAddressedOutputs, fixedCAOutputs, floatingCAOutputs, deferredIAOutputs,
        impureOutputs;
    std::optional<HashAlgorithm> floatingHashAlgo;

    for (auto & i : outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed &) { inputAddressedOutputs.insert(i.first); },
                [&](const DerivationOutput::CAFixed &) { fixedCAOutputs.insert(i.first); },
                [&](const DerivationOutput::CAFloating & dof) {
                    floatingCAOutputs.insert(i.first);
                    if (!floatingHashAlgo) {
                        floatingHashAlgo = dof.hashAlgo;
                    } else {
                        if (*floatingHashAlgo != dof.hashAlgo)
                            throw Error("all floating outputs must use the same hash algorithm");
                    }
                },
                [&](const DerivationOutput::Deferred &) { deferredIAOutputs.insert(i.first); },
                [&](const DerivationOutput::Impure &) { impureOutputs.insert(i.first); },
            },
            i.second.raw);
    }

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()
        && deferredIAOutputs.empty() && impureOutputs.empty())
        throw Error("must have at least one output");

    if (!inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()
        && deferredIAOutputs.empty() && impureOutputs.empty())
        return DerivationType::InputAddressed{
            .deferred = false,
        };

    if (inputAddressedOutputs.empty() && !fixedCAOutputs.empty() && floatingCAOutputs.empty()
        && deferredIAOutputs.empty() && impureOutputs.empty()) {
        if (fixedCAOutputs.size() > 1)
            // FIXME: Experimental feature?
            throw Error("only one fixed output is allowed for now");
        if (*fixedCAOutputs.begin() != "out"sv)
            throw Error("single fixed output must be named \"out\"");
        return DerivationType::ContentAddressed{
            .sandboxed = false,
            .fixed = true,
        };
    }

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && !floatingCAOutputs.empty()
        && deferredIAOutputs.empty() && impureOutputs.empty())
        return DerivationType::ContentAddressed{
            .sandboxed = true,
            .fixed = false,
        };

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()
        && !deferredIAOutputs.empty() && impureOutputs.empty())
        return DerivationType::InputAddressed{
            .deferred = true,
        };

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()
        && deferredIAOutputs.empty() && !impureOutputs.empty())
        return DerivationType::Impure{};

    throw Error("can't mix derivation output types");
}

DrvHashes drvHashes;

/* pathDerivationModulo and hashDerivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static const DrvHash pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    std::optional<DrvHash> hash;
    if (drvHashes.cvisit(drvPath, [&hash](const auto & kv) { hash.emplace(kv.second); })) {
        return *hash;
    }
    auto h = hashDerivationModulo(store, store.readInvalidDerivation(drvPath), false);
    // Cache it
    drvHashes.insert_or_assign(drvPath, h);
    return h;
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
DrvHash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs)
{
    auto type = drv.type();

    /* Return a fixed hash for fixed-output derivations. */
    if (type.isFixed()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<DerivationOutput::CAFixed>(i.second.raw);
            auto hash = hashString(
                HashAlgorithm::SHA256,
                "fixed:out:" + dof.ca.printMethodAlgo() + ":" + dof.ca.hash.to_string(HashFormat::Base16, false) + ":"
                    + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return DrvHash{
            .hashes = outputHashes,
            .kind = DrvHash::Kind::Regular,
        };
    }

    auto kind = std::visit(
        overloaded{
            [](const DerivationType::InputAddressed & ia) {
                /* This might be a "pesimistically" deferred output, so we don't
                   "taint" the kind yet. */
                return DrvHash::Kind::Regular;
            },
            [](const DerivationType::ContentAddressed & ca) {
                return ca.fixed ? DrvHash::Kind::Regular : DrvHash::Kind::Deferred;
            },
            [](const DerivationType::Impure &) -> DrvHash::Kind { return DrvHash::Kind::Deferred; }},
        drv.type().raw);

    DerivedPathMap<StringSet>::ChildNode::Map inputs2;
    for (auto & [drvPath, node] : drv.inputDrvs.map) {
        const auto & res = pathDerivationModulo(store, drvPath);
        if (res.kind == DrvHash::Kind::Deferred)
            kind = DrvHash::Kind::Deferred;
        for (auto & outputName : node.value) {
            const auto h = get(res.hashes, outputName);
            if (!h)
                throw Error("no hash for output '%s' of derivation '%s'", outputName, drv.name);
            inputs2[h->to_string(HashFormat::Base16, false)].value.insert(outputName);
        }
    }

    auto hash = hashString(HashAlgorithm::SHA256, drv.unparse(store, maskOutputs, &inputs2));

    std::map<std::string, Hash> outputHashes;
    for (const auto & [outputName, _] : drv.outputs) {
        outputHashes.insert_or_assign(outputName, hash);
    }

    return DrvHash{
        .hashes = outputHashes,
        .kind = kind,
    };
}

std::map<std::string, Hash> staticOutputHashes(Store & store, const Derivation & drv)
{
    return hashDerivationModulo(store, drv, true).hashes;
}

static DerivationOutput readDerivationOutput(Source & in, const StoreDirConfig & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseDerivationOutput(store, pathS, hashAlgo, hash, experimentalFeatureSettings);
}

StringSet BasicDerivation::outputNames() const
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}

DerivationOutputsAndOptPaths BasicDerivation::outputsAndOptPaths(const StoreDirConfig & store) const
{
    DerivationOutputsAndOptPaths outsAndOptPaths;
    for (auto & [outputName, output] : outputs)
        outsAndOptPaths.insert(
            std::make_pair(outputName, std::make_pair(output, output.path(store, name, outputName))));
    return outsAndOptPaths;
}

std::string_view BasicDerivation::nameFromPath(const StorePath & drvPath)
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

    drv.inputSrcs = CommonProto::Serialise<StorePathSet>::read(store, CommonProto::ReadConn{.from = in});
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
    CommonProto::write(store, CommonProto::WriteConn{.to = out}, drv.inputSrcs);
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

void BasicDerivation::applyRewrites(const StringMap & rewrites)
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

static void rewriteDerivation(Store & store, BasicDerivation & drv, const StringMap & rewrites)
{
    drv.applyRewrites(rewrites);

    auto hashModulo = hashDerivationModulo(store, Derivation(drv), true);
    for (auto & [outputName, output] : drv.outputs) {
        if (std::holds_alternative<DerivationOutput::Deferred>(output.raw)) {
            auto h = get(hashModulo.hashes, outputName);
            if (!h)
                throw Error(
                    "derivation '%s' output '%s' has no hash (derivations.cc/rewriteDerivation)", drv.name, outputName);
            auto outPath = store.makeOutputPath(outputName, *h, drv.name);
            drv.env[outputName] = store.printStorePath(outPath);
            output = DerivationOutput::InputAddressed{
                .path = std::move(outPath),
            };
        }
    }
}

std::optional<BasicDerivation> Derivation::tryResolve(Store & store, Store * evalStore) const
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

static bool tryResolveInput(
    Store & store,
    StorePathSet & inputSrcs,
    StringMap & inputRewrites,
    const DownstreamPlaceholder * placeholderOpt,
    ref<const SingleDerivedPath> drvPath,
    const DerivedPathMap<StringSet>::ChildNode & inputNode,
    std::function<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
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

std::optional<BasicDerivation> Derivation::tryResolve(
    Store & store,
    std::function<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const
{
    BasicDerivation resolved{*this};

    // Input paths that we'll want to rewrite in the derivation
    StringMap inputRewrites;

    for (auto & [inputDrv, inputNode] : inputDrvs.map)
        if (!tryResolveInput(
                store,
                resolved.inputSrcs,
                inputRewrites,
                nullptr,
                make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{inputDrv}),
                inputNode,
                queryResolutionChain))
            return std::nullopt;

    rewriteDerivation(store, resolved, inputRewrites);

    return resolved;
}

void Derivation::checkInvariants(Store & store, const StorePath & drvPath) const
{
    assert(drvPath.isDerivation());
    std::string drvName(drvPath.name());
    drvName = drvName.substr(0, drvName.size() - drvExtension.size());

    if (drvName != name) {
        throw Error("Derivation '%s' has name '%s' which does not match its path", store.printStorePath(drvPath), name);
    }

    auto envHasRightPath = [&](const StorePath & actual, const std::string & varName) {
        auto j = env.find(varName);
        if (j == env.end() || store.parseStorePath(j->second) != actual)
            throw Error(
                "derivation '%s' has incorrect environment variable '%s', should be '%s'",
                store.printStorePath(drvPath),
                varName,
                store.printStorePath(actual));
    };

    // Don't need the answer, but do this anyways to assert is proper
    // combination. The code below is more general and naturally allows
    // combinations that are currently prohibited.
    type();

    std::optional<DrvHash> hashesModulo;
    for (auto & i : outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed & doia) {
                    if (!hashesModulo) {
                        // somewhat expensive so we do lazily
                        hashesModulo = hashDerivationModulo(store, *this, true);
                    }
                    auto currentOutputHash = get(hashesModulo->hashes, i.first);
                    if (!currentOutputHash)
                        throw Error(
                            "derivation '%s' has unexpected output '%s' (local-store / hashesModulo) named '%s'",
                            store.printStorePath(drvPath),
                            store.printStorePath(doia.path),
                            i.first);
                    StorePath recomputed = store.makeOutputPath(i.first, *currentOutputHash, drvName);
                    if (doia.path != recomputed)
                        throw Error(
                            "derivation '%s' has incorrect output '%s', should be '%s'",
                            store.printStorePath(drvPath),
                            store.printStorePath(doia.path),
                            store.printStorePath(recomputed));
                    envHasRightPath(doia.path, i.first);
                },
                [&](const DerivationOutput::CAFixed & dof) {
                    auto path = dof.path(store, drvName, i.first);
                    envHasRightPath(path, i.first);
                },
                [&](const DerivationOutput::CAFloating &) {
                    /* Nothing to check */
                },
                [&](const DerivationOutput::Deferred &) {
                    /* Nothing to check */
                },
                [&](const DerivationOutput::Impure &) {
                    /* Nothing to check */
                },
            },
            i.second.raw);
    }
}

const Hash impureOutputHash = hashString(HashAlgorithm::SHA256, "impure");

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

static unsigned constexpr expectedJsonVersionDerivation = 4;

void adl_serializer<Derivation>::to_json(json & res, const Derivation & d)
{
    res = nlohmann::json::object();

    res["name"] = d.name;

    res["version"] = expectedJsonVersionDerivation;

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : d.outputs) {
            outputsObj[outputName] = output;
        }
    }

    {
        auto & inputsObj = res["inputs"];
        inputsObj = nlohmann::json::object();

        {
            auto & inputsList = inputsObj["srcs"];
            inputsList = nlohmann::json::array();
            for (auto & input : d.inputSrcs)
                inputsList.emplace_back(input);
        }

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

        auto & inputDrvsObj = inputsObj["drvs"];
        inputDrvsObj = nlohmann::json::object();
        for (auto & [inputDrv, inputNode] : d.inputDrvs.map) {
            inputDrvsObj[inputDrv.to_string()] = doInput(inputNode);
        }
    }

    res["system"] = d.platform;
    res["builder"] = d.builder;
    res["args"] = d.args;
    res["env"] = d.env;

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

Derivation adl_serializer<Derivation>::from_json(const json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    using nlohmann::detail::value_t;

    Derivation res;

    auto & json = getObject(_json);

    res.name = getString(valueAt(json, "name"));

    {
        auto version = getUnsigned(valueAt(json, "version"));
        if (valueAt(json, "version") != expectedJsonVersionDerivation)
            throw Error(
                "Unsupported derivation JSON format version %d, only format version %d is currently supported.",
                version,
                expectedJsonVersionDerivation);
    }

    try {
        auto outputs = getObject(valueAt(json, "outputs"));
        for (auto & [outputName, output] : outputs) {
            res.outputs.insert_or_assign(outputName, adl_serializer<DerivationOutput>::from_json(output, xpSettings));
        }
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'outputs'");
        throw;
    }

    try {
        auto inputsObj = getObject(valueAt(json, "inputs"));

        try {
            auto inputSrcs = getArray(valueAt(inputsObj, "srcs"));
            for (auto & input : inputSrcs)
                res.inputSrcs.insert(input);
        } catch (Error & e) {
            e.addTrace({}, "while reading key 'srcs'");
            throw;
        }

        try {
            auto doInput = [&](this const auto & doInput, const auto & _json) -> DerivedPathMap<StringSet>::ChildNode {
                auto & json = getObject(_json);
                DerivedPathMap<StringSet>::ChildNode node;
                node.value = getStringSet(valueAt(json, "outputs"));
                auto drvs = getObject(valueAt(json, "dynamicOutputs"));
                for (auto & [outputId, childNode] : drvs) {
                    xpSettings.require(
                        Xp::DynamicDerivations, [&] { return fmt("dynamic output '%s' in JSON", outputId); });
                    node.childMap[outputId] = doInput(childNode);
                }
                return node;
            };
            auto drvs = getObject(valueAt(inputsObj, "drvs"));
            for (auto & [inputDrvPath, inputOutputs] : drvs)
                res.inputDrvs.map[StorePath{inputDrvPath}] = doInput(inputOutputs);
        } catch (Error & e) {
            e.addTrace({}, "while reading key 'drvs'");
            throw;
        }
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'inputs'");
        throw;
    }

    res.platform = getString(valueAt(json, "system"));
    res.builder = getString(valueAt(json, "builder"));
    res.args = getStringList(valueAt(json, "args"));

    auto envJson = valueAt(json, "env");
    try {
        res.env = getStringMap(envJson);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'env'");
        throw;
    }

    if (auto structuredAttrs = get(json, "structuredAttrs"))
        res.structuredAttrs = StructuredAttrs{*structuredAttrs};

    return res;
}

} // namespace nlohmann
