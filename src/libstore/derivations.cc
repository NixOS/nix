#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "fs-accessor.hh"

namespace nix {

std::optional<StorePath> DerivationOutput::path(const Store & store, std::string_view drvName, std::string_view outputName) const
{
    return std::visit(overloaded {
        [](DerivationOutputInputAddressed doi) -> std::optional<StorePath> {
            return { doi.path };
        },
        [&](DerivationOutputCAFixed dof) -> std::optional<StorePath> {
            return {
                dof.path(store, drvName, outputName)
            };
        },
        [](DerivationOutputCAFloating dof) -> std::optional<StorePath> {
            return std::nullopt;
        },
        [](DerivationOutputDeferred) -> std::optional<StorePath> {
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
    case DerivationType::DeferredInputAddressed: return false;
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
    case DerivationType::DeferredInputAddressed: return false;
    };
    assert(false);
}

bool derivationHasKnownOutputPaths(DerivationType dt) {
    switch (dt) {
    case DerivationType::InputAddressed: return true;
    case DerivationType::CAFixed: return true;
    case DerivationType::CAFloating: return false;
    case DerivationType::DeferredInputAddressed: return false;
    };
    assert(false);
}


bool derivationIsImpure(DerivationType dt) {
    switch (dt) {
    case DerivationType::InputAddressed: return false;
    case DerivationType::CAFixed: return true;
    case DerivationType::CAFloating: return false;
    case DerivationType::DeferredInputAddressed: return false;
    };
    assert(false);
}


bool BasicDerivation::isBuiltin() const
{
    return string(builder, 0, 8) == "builtin:";
}


StorePath writeDerivation(Store & store,
    const Derivation & drv, RepairFlag repair, bool readOnly)
{
    auto references = drv.inputSrcs;
    for (auto & i : drv.inputDrvs)
        references.insert(i.first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = drv.unparse(store, false);
    return readOnly || settings.readOnlyMode
        ? store.computeStorePathForText(suffix, contents, references)
        : store.addTextToStore(suffix, contents, references, repair);
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
        if (pathS == "") {
            return DerivationOutput {
                .output = DerivationOutputDeferred { }
            };
        }
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


string Derivation::unparse(const Store & store, bool maskOutputs,
    std::map<std::string, StringSet> * actualInputs) const
{
    string s;
    s.reserve(65536);
    s += "Derive([";

    bool first = true;
    for (auto & i : outputs) {
        if (first) first = false; else s += ',';
        s += '('; printUnquotedString(s, i.first);
        std::visit(overloaded {
            [&](DerivationOutputInputAddressed doi) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : store.printStorePath(doi.path));
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            },
            [&](DerivationOutputCAFixed dof) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : store.printStorePath(dof.path(store, name, i.first)));
                s += ','; printUnquotedString(s, dof.hash.printMethodAlgo());
                s += ','; printUnquotedString(s, dof.hash.hash.to_string(Base16, false));
            },
            [&](DerivationOutputCAFloating dof) {
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType));
                s += ','; printUnquotedString(s, "");
            },
            [&](DerivationOutputDeferred) {
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            }
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
            s += '('; printUnquotedString(s, store.printStorePath(i.first));
            s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
            s += ')';
        }
    }

    s += "],";
    auto paths = store.printStorePathSet(inputSrcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ','; printUnquotedString(s, platform);
    s += ','; printString(s, builder);
    s += ','; printStrings(s, args.begin(), args.end());

    s += ",[";
    first = true;
    for (auto & i : env) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printString(s, maskOutputs && outputs.count(i.first) ? "" : i.second);
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


DerivationType BasicDerivation::type() const
{
    std::set<std::string_view> inputAddressedOutputs, fixedCAOutputs, floatingCAOutputs, deferredIAOutputs;
    std::optional<HashType> floatingHashType;
    for (auto & i : outputs) {
        std::visit(overloaded {
            [&](DerivationOutputInputAddressed _) {
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
            [&](DerivationOutputDeferred _) {
               deferredIAOutputs.insert(i.first);
            },
        }, i.second.output);
    }

    if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty() && deferredIAOutputs.empty()) {
        throw Error("Must have at least one output");
    } else if (! inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty() && deferredIAOutputs.empty()) {
        return DerivationType::InputAddressed;
    } else if (inputAddressedOutputs.empty() && ! fixedCAOutputs.empty() && floatingCAOutputs.empty() && deferredIAOutputs.empty()) {
        if (fixedCAOutputs.size() > 1)
            // FIXME: Experimental feature?
            throw Error("Only one fixed output is allowed for now");
        if (*fixedCAOutputs.begin() != "out")
            throw Error("Single fixed output must be named \"out\"");
        return DerivationType::CAFixed;
    } else if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && ! floatingCAOutputs.empty() && deferredIAOutputs.empty()) {
        return DerivationType::CAFloating;
    } else if (inputAddressedOutputs.empty() && fixedCAOutputs.empty() && floatingCAOutputs.empty() && !deferredIAOutputs.empty()) {
        return DerivationType::DeferredInputAddressed;
    } else {
        throw Error("Can't mix derivation output types");
    }
}


Sync<DrvHashes> drvHashes;

/* pathDerivationModulo and hashDerivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static const DrvHashModulo pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    {
        auto hashes = drvHashes.lock();
        auto h = hashes->find(drvPath);
        if (h != hashes->end()) {
            return h->second;
        }
    }
    auto h = hashDerivationModulo(
        store,
        store.readInvalidDerivation(drvPath),
        false);
    // Cache it
    drvHashes.lock()->insert_or_assign(drvPath, h);
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
DrvHashModulo hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs)
{
    bool isDeferred = false;
    /* Return a fixed hash for fixed-output derivations. */
    switch (drv.type()) {
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
    case DerivationType::CAFloating:
        isDeferred = true;
        break;
    case DerivationType::InputAddressed:
        break;
    case DerivationType::DeferredInputAddressed:
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
            [&](DeferredHash deferredHash) {
                isDeferred = true;
                inputs2.insert_or_assign(deferredHash.hash.to_string(Base16, false), i.second);
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

    auto hash = hashString(htSHA256, drv.unparse(store, maskOutputs, &inputs2));

    if (isDeferred)
        return DeferredHash { hash };
    else
        return hash;
}


std::map<std::string, Hash> staticOutputHashes(Store & store, const Derivation & drv)
{
    std::map<std::string, Hash> res;
    std::visit(overloaded {
        [&](Hash drvHash) {
            for (auto & outputName : drv.outputNames()) {
                res.insert({outputName, drvHash});
            }
        },
        [&](DeferredHash deferredHash) {
            for (auto & outputName : drv.outputNames()) {
                res.insert({outputName, deferredHash.hash});
            }
        },
        [&](CaOutputHashes outputHashes) {
            res = outputHashes;
        },
    }, hashDerivationModulo(store, drv, true));
    return res;
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

StringSet BasicDerivation::outputNames() const
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}

DerivationOutputsAndOptPaths BasicDerivation::outputsAndOptPaths(const Store & store) const {
    DerivationOutputsAndOptPaths outsAndOptPaths;
    for (auto output : outputs)
        outsAndOptPaths.insert(std::make_pair(
            output.first,
            std::make_pair(output.second, output.second.path(store, name, output.first))
            )
        );
    return outsAndOptPaths;
}

std::string_view BasicDerivation::nameFromPath(const StorePath & drvPath) {
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

    drv.inputSrcs = worker_proto::read(store, in, Phantom<StorePathSet> {});
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
            [&](DerivationOutputDeferred) {
                out << ""
                    << ""
                    << "";
            },
        }, i.second.output);
    }
    worker_proto::write(store, out, drv.inputSrcs);
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

std::string downstreamPlaceholder(const Store & store, const StorePath & drvPath, std::string_view outputName)
{
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    auto clearText = "nix-upstream-output:" + std::string { drvPath.hashPart() } + ":" + outputPathName(drvName, outputName);
    return "/" + hashString(htSHA256, clearText).to_string(Base32, false);
}


static void rewriteDerivation(Store & store, BasicDerivation & drv, const StringMap & rewrites) {

    debug("Rewriting the derivation");

    for (auto &rewrite: rewrites) {
        debug("rewriting %s as %s", rewrite.first, rewrite.second);
    }

    drv.builder = rewriteStrings(drv.builder, rewrites);
    for (auto & arg: drv.args) {
        arg = rewriteStrings(arg, rewrites);
    }

    StringPairs newEnv;
    for (auto & envVar: drv.env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second, rewrites);
        newEnv.emplace(envName, envValue);
    }
    drv.env = newEnv;

    auto hashModulo = hashDerivationModulo(store, Derivation(drv), true);
    for (auto & [outputName, output] : drv.outputs) {
        if (std::holds_alternative<DerivationOutputDeferred>(output.output)) {
            Hash h = std::get<Hash>(hashModulo);
            auto outPath = store.makeOutputPath(outputName, h, drv.name);
            drv.env[outputName] = store.printStorePath(outPath);
            output = DerivationOutput {
                .output = DerivationOutputInputAddressed {
                    .path = std::move(outPath),
                },
            };
        }
    }

}

std::optional<BasicDerivation> Derivation::tryResolve(Store & store) {
    BasicDerivation resolved { *this };

    // Input paths that we'll want to rewrite in the derivation
    StringMap inputRewrites;

    for (auto & input : inputDrvs) {
        auto inputDrvOutputs = store.queryPartialDerivationOutputMap(input.first);
        StringSet newOutputNames;
        for (auto & outputName : input.second) {
            auto actualPathOpt = inputDrvOutputs.at(outputName);
            if (!actualPathOpt) {
                warn("output %s of input %s missing, aborting the resolving",
                    outputName,
                    store.printStorePath(input.first)
                );
                return std::nullopt;
            }
            auto actualPath = *actualPathOpt;
            inputRewrites.emplace(
                downstreamPlaceholder(store, input.first, outputName),
                store.printStorePath(actualPath));
            resolved.inputSrcs.insert(std::move(actualPath));
        }
    }

    rewriteDerivation(store, resolved, inputRewrites);

    return resolved;
}

}
