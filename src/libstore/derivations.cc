#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "fs-accessor.hh"

namespace nix {

template<typename InputDrvPath, typename OutputPath>
DerivationT<InputDrvPath, OutputPath>::DerivationT(const BasicDerivationT<OutputPath> & other)
    : BasicDerivationT<OutputPath>(other)
{ }

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
    auto suffix = drv.name + drvExtension;
    auto contents = drv.unparse(*store);
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

static void validatePath(std::string_view s, Phantom<StorePath> _) {
    if (s.size() == 0 || s[0] != '/')
        throw FormatError("bad path '%1%' in derivation", s);
}

static void validatePath(std::string_view s, Phantom<NoPath> _) {
    if (s != "")
        throw FormatError("path '%1%' in unresolved derivation should be empty string", s);
}

static Path parsePath(std::istream & str)
{
    auto s = parseString(str);
    validatePath(s, Phantom<StorePath> {});
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


template<typename OutPath>
static DerivationOutputT<OutPath> parseDerivationOutput(const Store & store,
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
            validatePath(pathS, Phantom<OutPath> {});
            return DerivationOutputT<OutPath> {
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
            return DerivationOutputT<OutPath> {
                .output = DerivationOutputCAFloating {
                    .method = std::move(method),
                    .hashType = std::move(hashType),
                },
            };
        }
    } else {
        validatePath(pathS, Phantom<OutPath> {});
        return DerivationOutputT<OutPath> {
            .output = DerivationOutputInputAddressedT<OutPath> {
                .path = parseStorePath(store, pathS, Phantom<OutPath> {}),
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

    return parseDerivationOutput<StorePath>(store, pathS, hashAlgo, hash);
}


Derivation parseDerivation(const Store & store, std::string && s, std::string_view name)
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

template<typename T>
struct Phantom;

static string printStorePath(const Store & store, const StorePath & path) {
    return store.printStorePath(path);
}

static string printStorePath(const Store & store, const NoPath & _path) {
    return "";
}

static StorePath parseStorePath(const Store & store, std::string_view path, Phantom<StorePath>) {
    return store.parseStorePath(path);
}

static NoPath parseStorePath(const Store & store, std::string_view path, Phantom<NoPath>) {
    assert(path == "");
    return NoPath {};
}

static string printStoreDrvPath(const Store & store, const StorePath & path) {
    return store.printStorePath(path);
}

static string printStoreDrvPath(const Store & store, const Hash & hash) {
    return hash.to_string(Base16, false);
}

static StorePath parseStoreDrvPath(const Store & store, std::string_view path, Phantom<StorePath>) {
    return store.parseStorePath(path);
}

static Hash parseStoreDrvPath(const Store & store, std::string_view hash, Phantom<Hash>) {
    return Hash::parseNonSRIUnprefixed(hash, htSHA256);
}

template<typename InputDrvPath, typename OutputPath>
string DerivationT<InputDrvPath, OutputPath>::unparse(const Store & store) const
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
                s += ','; printUnquotedString(s, printStorePath(store, doi.path));
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            },
            [&](DerivationOutputCAFixed dof) {
                s += ','; printUnquotedString(s, store.printStorePath(dof.path(store, this->name, i.first)));
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
    for (auto & i : inputDrvs) {
        if (first) first = false; else s += ',';
        s += '('; printUnquotedString(s, printStoreDrvPath(store, i.first));
        s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
        s += ')';
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
        s += ','; printString(s, i.second);
        s += ')';
    }

    s += "])";

    return s;
}

template<typename InputDrvPath, typename OutPath>
Hash hashDerivation(Store & store, const DerivationT<InputDrvPath, OutPath> & drv) {
    return hashString(htSHA256, drv.unparse(store));
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

/* pathDerivationModulo and derivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static const DrvHashModulo & pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    auto h = drvHashes.find(drvPath);
    if (h == drvHashes.end()) {
        auto drv = store.readDerivation(drvPath);
        std::optional maybeOutputHashes = outputHashesForModuloIfFixed(store, drv);
        auto hashes = maybeOutputHashes
            ? DrvHashModulo { *maybeOutputHashes }
            /* For other derivations, replace the inputs paths with
               recursive calls to this function. */
            : hashDerivation(store, drv);
        h = drvHashes.insert_or_assign(drvPath, std::move(hashes)).first;
    }
    // Cache it
    return h->second;
}

template<typename OutPath>
DerivationT<Hash, OutPath> derivationModulo(
    Store & store,
    const DerivationT<StorePath, OutPath> & drv)
{
    DerivationT<Hash, OutPath> drvNorm { (const BasicDerivationT<OutPath> &)drv };
    for (auto & i : drv.inputDrvs) {
        std::visit(overloaded {
            // Regular non-CA derivation, replace derivation
            [&](Hash drvHash) {
                drvNorm.inputDrvs.insert_or_assign(drvHash, i.second);
            },
            // CA derivation's output hashes
            [&](CaOutputHashes outputHashes) {
                std::set<std::string> justOut = { "out" };
                for (auto & output : i.second) {
                    /* Put each one in with a single "out" output.. */
                    const auto h = outputHashes.at(output);
                    drvNorm.inputDrvs.insert_or_assign(h, justOut);
                }
            },
        }, pathDerivationModulo(store, i.first));
    }

    return drvNorm;
}

template<typename OutPath>
DerivationT<Hash, OutPath> derivationModulo(
    Store & store,
    const DerivationT<Hash, OutPath> & drv)
{
    return drv;
}

template<typename InputDrvPath, typename OutPath>
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<InputDrvPath, OutPath> & drv)
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
        return std::nullopt;
    }
    assert(false);
}

template<typename InputDrvPath>
DerivationT<InputDrvPath, StorePath> bakeDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, NoPath> & drv)
{
    DerivationT<InputDrvPath, StorePath> drvFinal;
    drvFinal.inputSrcs = drv.inputSrcs;
    drvFinal.platform = drv.platform;
    drvFinal.builder = drv.builder;
    drvFinal.args = drv.args;
    drvFinal.env = drv.env;
    drvFinal.name = drv.name;
    drvFinal.inputDrvs = drv.inputDrvs;

    std::optional<Hash> h;
    for (auto & [_outputName, output] : drv.outputs) {
        // Work around Clang 7 bug with destructuring and lambda capture.
        auto & outputName = _outputName;
        drvFinal.outputs.insert_or_assign(outputName, std::visit(overloaded {
            [&](DerivationOutputInputAddressedT<NoPath> doia) {
                if (!h) {
                    // somewhat expensive so we do lazily
                    DerivationT<Hash, NoPath> drvNorm = derivationModulo(store, drv);
                    h = hashDerivation(store, drvNorm);
                }
                return DerivationOutput {
                    .output = DerivationOutputInputAddressed {
                        .path = store.makeOutputPath(outputName, *h, drv.name),
                    },
                };
            },
            [&](DerivationOutputCAFixed dof) {
                return DerivationOutput { .output = dof };
            },
            [&](DerivationOutputCAFloating dof) {
                return DerivationOutput { .output = dof };
            },
        }, output.output));
    }

    for (auto & [outputName, output] : drvFinal.outputs) {
        auto pathOpt = output.pathOpt(store, drvFinal.name);
        drvFinal.env.insert_or_assign(
            outputName,
            pathOpt
                ? store.printStorePath(*pathOpt)
                : hashPlaceholder(outputName));
    }

    return drvFinal;
}

template<typename InputDrvPath>
DerivationT<InputDrvPath, NoPath> stripDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, StorePath> & drv)
{
    DerivationT<InputDrvPath, NoPath> drvInitial;
    drvInitial.inputSrcs = drv.inputSrcs;
    drvInitial.platform = drv.platform;
    drvInitial.builder = drv.builder;
    drvInitial.args = drv.args;
    drvInitial.env = drv.env;
    drvInitial.name = drv.name;
    drvInitial.inputDrvs = drv.inputDrvs;

    for (const auto & [outputName, output] : drv.outputs) {
        drvInitial.outputs.insert_or_assign(outputName, std::visit(overloaded {
            [&](DerivationOutputInputAddressed _) {
                return DerivationOutputT<NoPath> {
                    .output = DerivationOutputInputAddressedT<NoPath> {
                        .path = NoPath {},
                    },
                };
            },
            [&](DerivationOutputCAFixed dof) {
                return DerivationOutputT<NoPath> { .output = dof };
            },
            [&](DerivationOutputCAFloating dof) {
                return DerivationOutputT<NoPath> { .output = dof };
            },
        }, output.output));
        if (drvInitial.env.count(outputName)) {
            auto & envOutPath = drvInitial.env.at(outputName);
            auto optPath = output.pathOpt(store, drv.name);
            if (optPath && envOutPath == store.printStorePath(*optPath))
                envOutPath = "";
        }
    }
    return drvInitial;
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


template<typename OutPath>
static DerivationOutputT<OutPath> readDerivationOutput(Source & in, const Store & store)
{
    const auto pathS = readString(in);
    const auto hashAlgo = readString(in);
    const auto hash = readString(in);

    return parseDerivationOutput<OutPath>(store, pathS, hashAlgo, hash);
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


template<typename OutPath>
Source & readBasicDerivation(Source & in, const Store & store, BasicDerivationT<OutPath> & drv, std::optional<std::string_view> optName)
{
    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto name = readString(in);
        auto output = readDerivationOutput<OutPath>(in, store);
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

    if (optName)
        drv.name = *optName;
    else
        drv.name = readString(in);

    return in;
}

template<typename InputDrvPath, typename OutPath>
Source & readDerivation(Source & in, const Store & store, DerivationT<InputDrvPath, OutPath> & drv)
{
    readBasicDerivation(in, store, (DerivationT<InputDrvPath, OutPath> &) drv, std::nullopt);
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto key = parseStoreDrvPath(store, readString(in), Phantom<InputDrvPath> {});
        auto & wanted = drv.inputDrvs[key];
        auto kr = readNum<size_t>(in);
        for (size_t k = 0; k < kr; k++) 
            wanted.insert(readString(in));
    }

    return in;
}

template<typename OutPath>
void writeBasicDerivation(Sink & out, const Store & store, const BasicDerivationT<OutPath> & drv, bool includeName)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs) {
        out << i.first;
        std::visit(overloaded {
            [&](DerivationOutputInputAddressedT<OutPath> doi) {
                out << printStorePath(store, doi.path)
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
    if (includeName)
        out << drv.name;
}

template<typename InputDrvPath, typename OutPath>
void writeDerivation(Sink & out, const Store & store, const DerivationT<InputDrvPath, OutPath> & drv)
{
    writeBasicDerivation(out, store, (const BasicDerivationT<OutPath> &) drv, true);
    out << drv.inputDrvs.size();
    for (auto & i : drv.inputDrvs) {
        out << printStoreDrvPath(store, i.first);
        out << i.second;
    }
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
template struct DerivationT<Hash, StorePath>;
template struct DerivationT<Hash, NoPath>;

template
DerivationT<Hash, StorePath> derivationModulo(
    Store & store,
    const DerivationT<StorePath, StorePath> & drv);
template
DerivationT<Hash, NoPath> derivationModulo(
    Store & store,
    const DerivationT<StorePath, NoPath> & drv);
template
DerivationT<Hash, StorePath> derivationModulo(
    Store & store,
    const DerivationT<Hash, StorePath> & drv);
template
DerivationT<Hash, NoPath> derivationModulo(
    Store & store,
    const DerivationT<Hash, NoPath> & drv);

template
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<StorePath, StorePath> & drv);
template
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<Hash, StorePath> & drv);
template
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<StorePath, NoPath> & drv);
template
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<Hash, NoPath> & drv);

template Hash hashDerivation(Store & store, const DerivationT<StorePath, StorePath> & drv);
template Hash hashDerivation(Store & store, const DerivationT<StorePath, NoPath> & drv);
template Hash hashDerivation(Store & store, const DerivationT<Hash, StorePath> & drv);
template Hash hashDerivation(Store & store, const DerivationT<Hash, NoPath> & drv);

template
DerivationT<StorePath, StorePath> bakeDerivationPaths(
    Store & store,
    const DerivationT<StorePath, NoPath> & drv);
template
DerivationT<Hash, StorePath> bakeDerivationPaths(
    Store & store,
    const DerivationT<Hash, NoPath> & drv);

template
DerivationT<StorePath, NoPath> stripDerivationPaths(
    Store & store,
    const DerivationT<StorePath, StorePath> & drv);
template
DerivationT<Hash, NoPath> stripDerivationPaths(
    Store & store,
    const DerivationT<Hash, StorePath> & drv);

template
Source & readBasicDerivation(Source & in, const Store & store,
    BasicDerivationT<StorePath> & drv, std::optional<std::string_view> optName);

template
Source & readBasicDerivation(Source & in, const Store & store,
    BasicDerivationT<NoPath> & drv, std::optional<std::string_view> optName);

template
void writeBasicDerivation(Sink & out, const Store & store,
    const BasicDerivationT<StorePath> & drv, bool includeName);

template
void writeBasicDerivation(Sink & out, const Store & store,
    const BasicDerivationT<NoPath> & drv, bool includeName);

template
Source & readDerivation(Source & in, const Store & store, DerivationT<StorePath, StorePath> & drv);

template
Source & readDerivation(Source & in, const Store & store, DerivationT<StorePath, NoPath> & drv);

template
Source & readDerivation(Source & in, const Store & store, DerivationT<Hash, StorePath> & drv);

template
Source & readDerivation(Source & in, const Store & store, DerivationT<Hash, NoPath> & drv);

template
void writeDerivation(Sink & out, const Store & store, const DerivationT<StorePath, StorePath> & drv);

template
void writeDerivation(Sink & out, const Store & store, const DerivationT<StorePath, NoPath> & drv);

template
void writeDerivation(Sink & out, const Store & store, const DerivationT<Hash, StorePath> & drv);

template
void writeDerivation(Sink & out, const Store & store, const DerivationT<Hash, NoPath> & drv);

}
