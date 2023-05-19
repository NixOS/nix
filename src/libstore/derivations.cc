#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "fs-accessor.hh"

namespace nix {


template<>
std::optional<StorePath> DerivationOutputT<NoPath>::pathOpt(const Store & store, std::string_view drvName) const
{
    return std::nullopt;
}

template<>
std::optional<StorePath> DerivationOutput::pathOpt(const Store & store, std::string_view drvName) const
{
    return std::visit(overloaded {
        [](DerivationOutputInputAddressed doi) -> std::optional<StorePath> {
            return { doi.path };
        },
        [&](DerivationOutputCAFixed dof) -> std::optional<StorePath> {
            return {
                // FIXME if we intend to support multiple CA outputs.
                dof.path(store, drvName, "out")
            };
        },
        [](DerivationOutputCAFloating dof) -> std::optional<StorePath> {
            return std::nullopt;
        },
    }, output);
}


StorePath DerivationOutputCAFixed::path(const Store & store, std::string_view drvName, std::string_view outputName) const {
    return store.makeFixedOutputPath(
        hash.method, hash.hash,
        outputPathName(drvName, outputName));
}


bool derivationIsCA(DerivationType dt) {
    switch (dt) {
    case DerivationType::InputAddressed: return false;
    case DerivationType::CAFixed: return true;
    case DerivationType::CAFloating: return true;
    };
    // Since enums can have non-variant values, but making a `default:` would
    // disable exhaustiveness warnings.
    assert(false);
}

bool derivationIsFixed(DerivationType dt) {
    switch (dt) {
    case DerivationType::InputAddressed: return false;
    case DerivationType::CAFixed: return true;
    case DerivationType::CAFloating: return false;
    };
    assert(false);
}

bool derivationIsImpure(DerivationType dt) {
    switch (dt) {
    case DerivationType::InputAddressed: return false;
    case DerivationType::CAFixed: return true;
    case DerivationType::CAFloating: return false;
    };
    assert(false);
}


template<typename Path>
bool BasicDerivationT<Path>::isBuiltin() const
{
    return string(builder, 0, 8) == "builtin:";
}


StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, RepairFlag repair)
{
    auto references = drv.inputSrcs;
    for (auto & i : drv.inputDrvs)
        references.insert(i.first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = drv.unparse(*store, false);
    return settings.readOnlyMode
        ? store->computeStorePathForText(suffix, contents, references)
        : store->addTextToStore(suffix, contents, references, repair);
}


/* Read string `s' from stream `str'. */
static void expect(std::istream & str, const string & s)
{
    char s2[s.size()];
    str.read(s2, s.size());
    if (string(s2, s.size()) != s)
        throw FormatError("expected string '%1%'", s);
}


/* Read a C-style string from stream `str'. */
static string parseString(std::istream & str)
{
    string res;
    expect(str, "\"");
    int c;
    while ((c = str.get()) != '"')
        if (c == '\\') {
            c = str.get();
            if (c == 'n') res += '\n';
            else if (c == 'r') res += '\r';
            else if (c == 't') res += '\t';
            else res += c;
        }
        else res += c;
    return res;
}

static void validatePath(std::string_view s) {
    if (s.size() == 0 || s[0] != '/')
        throw FormatError("bad path '%1%' in derivation", s);
}

static Path parsePath(std::istream & str)
{
    auto s = parseString(str);
    validatePath(s);
    return s;
}


static bool endOfList(std::istream & str)
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


static StringSet parseStrings(std::istream & str, bool arePaths)
{
    StringSet res;
    while (!endOfList(str))
        res.insert(arePaths ? parsePath(str) : parseString(str));
    return res;
}


static DerivationOutput parseDerivationOutput(const Store & store,
    std::string_view pathS, std::string_view hashAlgo, std::string_view hash)
{
    if (hashAlgo != "") {
        auto method = FileIngestionMethod::Flat;
        if (string(hashAlgo, 0, 2) == "r:") {
            method = FileIngestionMethod::Recursive;
            hashAlgo = hashAlgo.substr(2);
        }
        const auto hashType = parseHashType(hashAlgo);
        if (hash != "") {
            validatePath(pathS);
            return DerivationOutput {
                .output = DerivationOutputCAFixed {
                    .hash = FixedOutputHash {
                        .method = std::move(method),
                        .hash = Hash::parseNonSRIUnprefixed(hash, hashType),
                    },
                },
            };
        } else {
            settings.requireExperimentalFeature("ca-derivations");
            assert(pathS == "");
            return DerivationOutput {
                .output = DerivationOutputCAFloating {
                    .method = std::move(method),
                    .hashType = std::move(hashType),
                },
            };
        }
    } else {
        validatePath(pathS);
        return DerivationOutput {
            .output = DerivationOutputInputAddressed {
                .path = store.parseStorePath(pathS),
            }
        };
    }
}

static DerivationOutput parseDerivationOutput(const Store & store, std::istringstream & str)
{
    expect(str, ","); const auto pathS = parseString(str);
    expect(str, ","); const auto hashAlgo = parseString(str);
    expect(str, ","); const auto hash = parseString(str);
    expect(str, ")");

    return parseDerivationOutput(store, pathS, hashAlgo, hash);
}


static Derivation parseDerivation(const Store & store, std::string && s, std::string_view name)
{
    Derivation drv;
    drv.name = name;

    std::istringstream str(std::move(s));
    expect(str, "Derive([");

    /* Parse the list of outputs. */
    while (!endOfList(str)) {
        expect(str, "("); std::string id = parseString(str);
        auto output = parseDerivationOutput(store, str);
        drv.outputs.emplace(std::move(id), std::move(output));
    }

    /* Parse the list of input derivations. */
    expect(str, ",[");
    while (!endOfList(str)) {
        expect(str, "(");
        Path drvPath = parsePath(str);
        expect(str, ",[");
        drv.inputDrvs.insert_or_assign(store.parseStorePath(drvPath), parseStrings(str, false));
        expect(str, ")");
    }

    expect(str, ",["); drv.inputSrcs = store.parseStorePathSet(parseStrings(str, true));
    expect(str, ","); drv.platform = parseString(str);
    expect(str, ","); drv.builder = parseString(str);

    /* Parse the builder arguments. */
    expect(str, ",[");
    while (!endOfList(str))
        drv.args.push_back(parseString(str));

    /* Parse the environment variables. */
    expect(str, ",[");
    while (!endOfList(str)) {
        expect(str, "("); string name = parseString(str);
        expect(str, ","); string value = parseString(str);
        expect(str, ")");
        drv.env[name] = value;
    }

    expect(str, ")");
    return drv;
}


Derivation readDerivation(const Store & store, const Path & drvPath, std::string_view name)
{
    try {
        return parseDerivation(store, readFile(drvPath), name);
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%1%': %2%", drvPath, e.msg());
    }
}


Derivation Store::derivationFromPath(const StorePath & drvPath)
{
    ensurePath(drvPath);
    return readDerivation(drvPath);
}


Derivation Store::readDerivation(const StorePath & drvPath)
{
    auto accessor = getFSAccessor();
    try {
        return parseDerivation(*this, accessor->readFile(printStorePath(drvPath)), Derivation::nameFromPath(drvPath));
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", printStorePath(drvPath), e.msg());
    }
}


static void printString(string & res, std::string_view s)
{
    char buf[s.size() * 2 + 2];
    char * p = buf;
    *p++ = '"';
    for (auto c : s)
        if (c == '\"' || c == '\\') { *p++ = '\\'; *p++ = c; }
        else if (c == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (c == '\r') { *p++ = '\\'; *p++ = 'r'; }
        else if (c == '\t') { *p++ = '\\'; *p++ = 't'; }
        else *p++ = c;
    *p++ = '"';
    res.append(buf, p - buf);
}


static void printUnquotedString(string & res, std::string_view s)
{
    res += '"';
    res.append(s);
    res += '"';
}


template<class ForwardIterator>
static void printStrings(string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printString(res, *i);
    }
    res += ']';
}


template<class ForwardIterator>
static void printUnquotedStrings(string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printUnquotedString(res, *i);
    }
    res += ']';
}

static string printStorePath(const Store & store, const StorePath & path) {
    return store.printStorePath(path);
}

static string printStorePath(const Store & store, const NoPath & _path) {
    return "";
}

static string printStoreDrvPath(const Store & store, const StorePath & path) {
    return store.printStorePath(path);
}

static string printStoreDrvPath(const Store & store, const Hash & hash) {
    return hash.to_string(Base16, false);
}

template<typename InputDrvPath, typename OutputPath>
string DerivationT<InputDrvPath, OutputPath>::unparse(const Store & store, bool maskOutputs,
    std::map<std::string, StringSet> * actualInputs) const
{
    string s;
    s.reserve(65536);
    s += "Derive([";

    bool first = true;
    for (auto & i : this->outputs) {
        if (first) first = false; else s += ',';
        s += '('; printUnquotedString(s, i.first);
        std::visit(overloaded {
            [&](DerivationOutputInputAddressedT<OutputPath> doi) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : printStorePath(store, doi.path));
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            },
            [&](DerivationOutputCAFixed dof) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : store.printStorePath(dof.path(store, this->name, i.first)));
                s += ','; printUnquotedString(s, dof.hash.printMethodAlgo());
                s += ','; printUnquotedString(s, dof.hash.hash.to_string(Base16, false));
            },
            [&](DerivationOutputCAFloating dof) {
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType));
                s += ','; printUnquotedString(s, "");
            },
        }, i.second.output);
        s += ')';
    }

    s += "],[";
    first = true;
    if (actualInputs) {
        for (auto & i : *actualInputs) {
            if (first) first = false; else s += ',';
            s += '('; printUnquotedString(s, i.first);
            s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
            s += ')';
        }
    } else {
        for (auto & i : inputDrvs) {
            if (first) first = false; else s += ',';
            s += '('; printUnquotedString(s, printStoreDrvPath(store, i.first));
            s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
            s += ')';
        }
    }

    s += "],";
    auto paths = store.printStorePathSet(this->inputSrcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ','; printUnquotedString(s, this->platform);
    s += ','; printString(s, this->builder);
    s += ','; printStrings(s, this->args.begin(), this->args.end());

    s += ",[";
    first = true;
    for (auto & i : this->env) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printString(s, maskOutputs && this->outputs.count(i.first) ? "" : i.second);
        s += ')';
    }

    s += "])";

    return s;
}


// FIXME: remove
bool isDerivation(const string & fileName)
{
    return hasSuffix(fileName, drvExtension);
}


std::string outputPathName(std::string_view drvName, std::string_view outputName) {
    std::string res { drvName };
    if (outputName != "out") {
        res += "-";
        res += outputName;
    }
    return res;
}


template<typename Path>
DerivationType BasicDerivationT<Path>::type() const
{
    std::set<std::string_view> inputAddressedOutputs, fixedCAOutputs, floatingCAOutputs;
    std::optional<HashType> floatingHashType;
    for (auto & i : outputs) {
        std::visit(overloaded {
            [&](DerivationOutputInputAddressedT<Path> _) {
               inputAddressedOutputs.insert(i.first);
            },
            [&](DerivationOutputCAFixed _) {
                fixedCAOutputs.insert(i.first);
            },
            [&](DerivationOutputCAFloating dof) {
                floatingCAOutputs.insert(i.first);
                if (!floatingHashType) {
                    floatingHashType = dof.hashType;
                } else {
                    if (*floatingHashType != dof.hashType)
                        throw Error("All floating outputs must use the same hash type");
                }
            },
        }, i.second.output);
    }

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()) {
        throw Error("Must have at least one output");
    } else if (! inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty()) {
        return DerivationType::InputAddressed;
    } else if (inputAddressedOutputs.empty() && ! fixedCAOutputs.empty() && floatingCAOutputs.empty()) {
        if (fixedCAOutputs.size() > 1)
            // FIXME: Experimental feature?
            throw Error("Only one fixed output is allowed for now");
        if (*fixedCAOutputs.begin() != "out")
            throw Error("Single fixed output must be named \"out\"");
        return DerivationType::CAFixed;
    } else if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && ! floatingCAOutputs.empty()) {
        return DerivationType::CAFloating;
    } else {
        throw Error("Can't mix derivation output types");
    }
}


DrvHashes drvHashes;

/* pathDerivationModulo and hashDerivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static const DrvHashModulo & pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    auto h = drvHashes.find(drvPath);
    if (h == drvHashes.end()) {
        assert(store.isValidPath(drvPath));
        // Cache it
        h = drvHashes.insert_or_assign(
            drvPath,
            hashDerivationModulo(
                store,
                store.readDerivation(drvPath),
                false)).first;
    }
    return h->second;
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
DrvHashModulo hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs)
{
    /* Return a fixed hash for fixed-output derivations. */
    switch (drv.type()) {
    case DerivationType::CAFloating:
        throw Error("Regular input-addressed derivations are not yet allowed to depend on CA derivations");
    case DerivationType::CAFixed: {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<DerivationOutputCAFixed>(i.second.output);
            auto hash = hashString(htSHA256, "fixed:out:"
                + dof.hash.printMethodAlgo() + ":"
                + dof.hash.hash.to_string(Base16, false) + ":"
                + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return outputHashes;
    }
    case DerivationType::InputAddressed:
        break;
    }

    /* For other derivations, replace the inputs paths with recursive
       calls to this function. */
    std::map<std::string, StringSet> inputs2;
    for (auto & i : drv.inputDrvs) {
        const auto & res = pathDerivationModulo(store, i.first);
        std::visit(overloaded {
            // Regular non-CA derivation, replace derivation
            [&](Hash drvHash) {
                inputs2.insert_or_assign(drvHash.to_string(Base16, false), i.second);
            },
            // CA derivation's output hashes
            [&](CaOutputHashes outputHashes) {
                std::set<std::string> justOut = { "out" };
                for (auto & output : i.second) {
                    /* Put each one in with a single "out" output.. */
                    const auto h = outputHashes.at(output);
                    inputs2.insert_or_assign(
                        h.to_string(Base16, false),
                        justOut);
                }
            },
        }, res);
    }

    return hashString(htSHA256, drv.unparse(store, maskOutputs, &inputs2));
}


std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


bool wantOutput(const string & output, const std::set<string> & wanted)
{
    return wanted.empty() || wanted.find(output) != wanted.end();
}


static DerivationOutput readDerivationOutput(Source & in, const Store & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseDerivationOutput(store, pathS, hashAlgo, hash);
}

template<typename Path>
StringSet BasicDerivationT<Path>::outputNames() const
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}


template<typename Path>
std::string_view BasicDerivationT<Path>::nameFromPath(const StorePath & drvPath) {
    auto nameWithSuffix = drvPath.name();
    constexpr std::string_view extension = ".drv";
    assert(hasSuffix(nameWithSuffix, extension));
    nameWithSuffix.remove_suffix(extension.size());
    return nameWithSuffix;
}


Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv, std::string_view name)
{
    drv.name = name;

    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto name = readString(in);
        auto output = readDerivationOutput(in, store);
        drv.outputs.emplace(std::move(name), std::move(output));
    }

    drv.inputSrcs = readStorePaths<StorePathSet>(store, in);
    in >> drv.platform >> drv.builder;
    drv.args = readStrings<Strings>(in);

    nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto key = readString(in);
        auto value = readString(in);
        drv.env[key] = value;
    }

    return in;
}


void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs) {
        out << i.first;
        std::visit(overloaded {
            [&](DerivationOutputInputAddressed doi) {
                out << store.printStorePath(doi.path)
                    << ""
                    << "";
            },
            [&](DerivationOutputCAFixed dof) {
                out << store.printStorePath(dof.path(store, drv.name, i.first))
                    << dof.hash.printMethodAlgo()
                    << dof.hash.hash.to_string(Base16, false);
            },
            [&](DerivationOutputCAFloating dof) {
                out << ""
                    << (makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType))
                    << "";
            },
        }, i.second.output);
    }
    writeStorePaths(store, out, drv.inputSrcs);
    out << drv.platform << drv.builder << drv.args;
    out << drv.env.size();
    for (auto & i : drv.env)
        out << i.first << i.second;
}


std::string hashPlaceholder(const std::string & outputName)
{
    // FIXME: memoize?
    return "/" + hashString(htSHA256, "nix-output:" + outputName).to_string(Base32, false);
}

StorePath downstreamPlaceholder(const Store & store, const StorePath & drvPath, std::string_view outputName)
{
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    return store.makeStorePath(
        "downstream-placeholder:" + std::string { drvPath.name() } + ":" + std::string { outputName },
        "compressed:" + std::string { drvPath.hashPart() },
        outputPathName(drvName, outputName));
}

template struct DerivationOutputT<StorePath>;
template struct DerivationOutputT<NoPath>;

template struct BasicDerivationT<StorePath>;
template struct BasicDerivationT<NoPath>;

template struct DerivationT<StorePath, StorePath>;
template struct DerivationT<Hash, NoPath>;

}
