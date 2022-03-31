#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "fs-accessor.hh"
#include <boost/container/small_vector.hpp>

namespace nix {

std::optional<StorePath> DerivationOutput::path(const Store & store, std::string_view drvName, std::string_view outputName) const
{
    return std::visit(overloaded {
        [](const DerivationOutput::InputAddressed & doi) -> std::optional<StorePath> {
            return { doi.path };
        },
        [&](const DerivationOutput::CAFixed & dof) -> std::optional<StorePath> {
            return {
                dof.path(store, drvName, outputName)
            };
        },
        [](const DerivationOutput::CAFloating & dof) -> std::optional<StorePath> {
            return std::nullopt;
        },
        [](const DerivationOutput::Deferred &) -> std::optional<StorePath> {
            return std::nullopt;
        },
        [](const DerivationOutput::Impure &) -> std::optional<StorePath> {
            return std::nullopt;
        },
    }, raw());
}


StorePath DerivationOutput::CAFixed::path(const Store & store, std::string_view drvName, std::string_view outputName) const
{
    return store.makeFixedOutputPath(
        hash.method, hash.hash,
        outputPathName(drvName, outputName));
}


bool DerivationType::isCA() const
{
    /* Normally we do the full `std::visit` to make sure we have
       exhaustively handled all variants, but so long as there is a
       variant called `ContentAddressed`, it must be the only one for
       which `isCA` is true for this to make sense!. */
    return std::visit(overloaded {
        [](const InputAddressed & ia) {
            return false;
        },
        [](const ContentAddressed & ca) {
            return true;
        },
        [](const Impure &) {
            return true;
        },
    }, raw());
}

bool DerivationType::isFixed() const
{
    return std::visit(overloaded {
        [](const InputAddressed & ia) {
            return false;
        },
        [](const ContentAddressed & ca) {
            return ca.fixed;
        },
        [](const Impure &) {
            return false;
        },
    }, raw());
}

bool DerivationType::hasKnownOutputPaths() const
{
    return std::visit(overloaded {
        [](const InputAddressed & ia) {
            return !ia.deferred;
        },
        [](const ContentAddressed & ca) {
            return ca.fixed;
        },
        [](const Impure &) {
            return false;
        },
    }, raw());
}


bool DerivationType::isSandboxed() const
{
    return std::visit(overloaded {
        [](const InputAddressed & ia) {
            return true;
        },
        [](const ContentAddressed & ca) {
            return ca.sandboxed;
        },
        [](const Impure &) {
            return false;
        },
    }, raw());
}


bool DerivationType::isPure() const
{
    return std::visit(overloaded {
        [](const InputAddressed & ia) {
            return true;
        },
        [](const ContentAddressed & ca) {
            return true;
        },
        [](const Impure &) {
            return false;
        },
    }, raw());
}


bool BasicDerivation::isBuiltin() const
{
    return builder.substr(0, 8) == "builtin:";
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
static void expect(std::istream & str, std::string_view s)
{
    char s2[s.size()];
    str.read(s2, s.size());
    if (std::string(s2, s.size()) != s)
        throw FormatError("expected string '%1%'", s);
}


/* Read a C-style string from stream `str'. */
static std::string parseString(std::istream & str)
{
    std::string res;
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
        if (hashAlgo.substr(0, 2) == "r:") {
            method = FileIngestionMethod::Recursive;
            hashAlgo = hashAlgo.substr(2);
        }
        const auto hashType = parseHashType(hashAlgo);
        if (hash == "impure") {
            settings.requireExperimentalFeature(Xp::ImpureDerivations);
            assert(pathS == "");
            return DerivationOutput::Impure {
                .method = std::move(method),
                .hashType = std::move(hashType),
            };
        } else if (hash != "") {
            validatePath(pathS);
            return DerivationOutput::CAFixed {
                .hash = FixedOutputHash {
                    .method = std::move(method),
                    .hash = Hash::parseNonSRIUnprefixed(hash, hashType),
                },
            };
        } else {
            settings.requireExperimentalFeature(Xp::CaDerivations);
            assert(pathS == "");
            return DerivationOutput::CAFloating {
                .method = std::move(method),
                .hashType = std::move(hashType),
            };
        }
    } else {
        if (pathS == "") {
            return DerivationOutput::Deferred { };
        }
        validatePath(pathS);
        return DerivationOutput::InputAddressed {
            .path = store.parseStorePath(pathS),
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
        expect(str, "("); auto name = parseString(str);
        expect(str, ","); auto value = parseString(str);
        expect(str, ")");
        drv.env[name] = value;
    }

    expect(str, ")");
    return drv;
}


static void printString(std::string & res, std::string_view s)
{
    boost::container::small_vector<char, 64 * 1024> buffer;
    buffer.reserve(s.size() * 2 + 2);
    char * buf = buffer.data();
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
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printString(res, *i);
    }
    res += ']';
}


template<class ForwardIterator>
static void printUnquotedStrings(std::string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printUnquotedString(res, *i);
    }
    res += ']';
}


std::string Derivation::unparse(const Store & store, bool maskOutputs,
    std::map<std::string, StringSet> * actualInputs) const
{
    std::string s;
    s.reserve(65536);
    s += "Derive([";

    bool first = true;
    for (auto & i : outputs) {
        if (first) first = false; else s += ',';
        s += '('; printUnquotedString(s, i.first);
        std::visit(overloaded {
            [&](const DerivationOutput::InputAddressed & doi) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : store.printStorePath(doi.path));
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            },
            [&](const DerivationOutput::CAFixed & dof) {
                s += ','; printUnquotedString(s, maskOutputs ? "" : store.printStorePath(dof.path(store, name, i.first)));
                s += ','; printUnquotedString(s, dof.hash.printMethodAlgo());
                s += ','; printUnquotedString(s, dof.hash.hash.to_string(Base16, false));
            },
            [&](const DerivationOutput::CAFloating & dof) {
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType));
                s += ','; printUnquotedString(s, "");
            },
            [&](const DerivationOutput::Deferred &) {
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, "");
            },
            [&](const DerivationOutputImpure & doi) {
                // FIXME
                s += ','; printUnquotedString(s, "");
                s += ','; printUnquotedString(s, makeFileIngestionPrefix(doi.method) + printHashType(doi.hashType));
                s += ','; printUnquotedString(s, "impure");
            }
        }, i.second.raw());
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
bool isDerivation(const std::string & fileName)
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
    std::set<std::string_view>
        inputAddressedOutputs,
        fixedCAOutputs,
        floatingCAOutputs,
        deferredIAOutputs,
        impureOutputs;
    std::optional<HashType> floatingHashType;

    for (auto & i : outputs) {
        std::visit(overloaded {
            [&](const DerivationOutput::InputAddressed &) {
               inputAddressedOutputs.insert(i.first);
            },
            [&](const DerivationOutput::CAFixed &) {
                fixedCAOutputs.insert(i.first);
            },
            [&](const DerivationOutput::CAFloating & dof) {
                floatingCAOutputs.insert(i.first);
                if (!floatingHashType) {
                    floatingHashType = dof.hashType;
                } else {
                    if (*floatingHashType != dof.hashType)
                        throw Error("all floating outputs must use the same hash type");
                }
            },
            [&](const DerivationOutput::Deferred &) {
                deferredIAOutputs.insert(i.first);
            },
            [&](const DerivationOutput::Impure &) {
                impureOutputs.insert(i.first);
            },
        }, i.second.raw());
    }

    if (inputAddressedOutputs.empty()
        && fixedCAOutputs.empty()
        && floatingCAOutputs.empty()
        && deferredIAOutputs.empty()
        && impureOutputs.empty())
        throw Error("must have at least one output");

    if (!inputAddressedOutputs.empty()
        && fixedCAOutputs.empty()
        && floatingCAOutputs.empty()
        && deferredIAOutputs.empty()
        && impureOutputs.empty())
        return DerivationType::InputAddressed {
            .deferred = false,
        };

    if (inputAddressedOutputs.empty()
        && !fixedCAOutputs.empty()
        && floatingCAOutputs.empty()
        && deferredIAOutputs.empty()
        && impureOutputs.empty())
    {
        if (fixedCAOutputs.size() > 1)
            // FIXME: Experimental feature?
            throw Error("only one fixed output is allowed for now");
        if (*fixedCAOutputs.begin() != "out")
            throw Error("single fixed output must be named \"out\"");
        return DerivationType::ContentAddressed {
            .sandboxed = false,
            .fixed = true,
        };
    }

    if (inputAddressedOutputs.empty()
        && fixedCAOutputs.empty()
        && !floatingCAOutputs.empty()
        && deferredIAOutputs.empty()
        && impureOutputs.empty())
        return DerivationType::ContentAddressed {
            .sandboxed = true,
            .fixed = false,
        };

    if (inputAddressedOutputs.empty()
        && fixedCAOutputs.empty()
        && floatingCAOutputs.empty()
        && !deferredIAOutputs.empty()
        && impureOutputs.empty())
        return DerivationType::InputAddressed {
            .deferred = true,
        };

    if (inputAddressedOutputs.empty()
        && fixedCAOutputs.empty()
        && floatingCAOutputs.empty()
        && deferredIAOutputs.empty()
        && !impureOutputs.empty())
        return DerivationType::Impure { };

    throw Error("can't mix derivation output types");
}


Sync<DrvHashes> drvHashes;

/* pathDerivationModulo and hashDerivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static const DrvHash pathDerivationModulo(Store & store, const StorePath & drvPath)
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
DrvHash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs)
{
    auto type = drv.type();

    /* Return a fixed hash for fixed-output derivations. */
    if (type.isFixed()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<DerivationOutput::CAFixed>(i.second.raw());
            auto hash = hashString(htSHA256, "fixed:out:"
                + dof.hash.printMethodAlgo() + ":"
                + dof.hash.hash.to_string(Base16, false) + ":"
                + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return DrvHash {
            .hashes = outputHashes,
            .kind = DrvHash::Kind::Regular,
        };
    }

    if (!type.isPure()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & [outputName, _] : drv.outputs)
            outputHashes.insert_or_assign(outputName, impureOutputHash);
        return DrvHash {
            .hashes = outputHashes,
            .kind = DrvHash::Kind::Deferred,
        };
    }

    auto kind = std::visit(overloaded {
        [](const DerivationType::InputAddressed & ia) {
            /* This might be a "pesimistically" deferred output, so we don't
               "taint" the kind yet. */
            return DrvHash::Kind::Regular;
        },
        [](const DerivationType::ContentAddressed & ca) {
            return ca.fixed
                ? DrvHash::Kind::Regular
                : DrvHash::Kind::Deferred;
        },
        [](const DerivationType::Impure &) -> DrvHash::Kind {
            assert(false);
        }
    }, drv.type().raw());

    std::map<std::string, StringSet> inputs2;
    for (auto & [drvPath, inputOutputs0] : drv.inputDrvs) {
        // Avoid lambda capture restriction with standard / Clang
        auto & inputOutputs = inputOutputs0;
        const auto & res = pathDerivationModulo(store, drvPath);
        if (res.kind == DrvHash::Kind::Deferred)
            kind = DrvHash::Kind::Deferred;
        for (auto & outputName : inputOutputs) {
            const auto h = res.hashes.at(outputName);
            inputs2[h.to_string(Base16, false)].insert(outputName);
        }
    }

    auto hash = hashString(htSHA256, drv.unparse(store, maskOutputs, &inputs2));

    std::map<std::string, Hash> outputHashes;
    for (const auto & [outputName, _] : drv.outputs) {
        outputHashes.insert_or_assign(outputName, hash);
    }

    return DrvHash {
        .hashes = outputHashes,
        .kind = kind,
    };
}


std::map<std::string, Hash> staticOutputHashes(Store & store, const Derivation & drv)
{
    return hashDerivationModulo(store, drv, true).hashes;
}


bool wantOutput(const std::string & output, const std::set<std::string> & wanted)
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

DerivationOutputsAndOptPaths BasicDerivation::outputsAndOptPaths(const Store & store) const
{
    DerivationOutputsAndOptPaths outsAndOptPaths;
    for (auto output : outputs)
        outsAndOptPaths.insert(std::make_pair(
            output.first,
            std::make_pair(output.second, output.second.path(store, name, output.first))
            )
        );
    return outsAndOptPaths;
}

std::string_view BasicDerivation::nameFromPath(const StorePath & drvPath)
{
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
            [&](const DerivationOutput::InputAddressed & doi) {
                out << store.printStorePath(doi.path)
                    << ""
                    << "";
            },
            [&](const DerivationOutput::CAFixed & dof) {
                out << store.printStorePath(dof.path(store, drv.name, i.first))
                    << dof.hash.printMethodAlgo()
                    << dof.hash.hash.to_string(Base16, false);
            },
            [&](const DerivationOutput::CAFloating & dof) {
                out << ""
                    << (makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType))
                    << "";
            },
            [&](const DerivationOutput::Deferred &) {
                out << ""
                    << ""
                    << "";
            },
            [&](const DerivationOutput::Impure & doi) {
                out << ""
                    << (makeFileIngestionPrefix(doi.method) + printHashType(doi.hashType))
                    << "impure";
            },
        }, i.second.raw());
    }
    worker_proto::write(store, out, drv.inputSrcs);
    out << drv.platform << drv.builder << drv.args;
    out << drv.env.size();
    for (auto & i : drv.env)
        out << i.first << i.second;
}


std::string hashPlaceholder(const std::string_view outputName)
{
    // FIXME: memoize?
    return "/" + hashString(htSHA256, concatStrings("nix-output:", outputName)).to_string(Base32, false);
}

std::string downstreamPlaceholder(const Store & store, const StorePath & drvPath, std::string_view outputName)
{
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    auto clearText = "nix-upstream-output:" + std::string { drvPath.hashPart() } + ":" + outputPathName(drvName, outputName);
    return "/" + hashString(htSHA256, clearText).to_string(Base32, false);
}


static void rewriteDerivation(Store & store, BasicDerivation & drv, const StringMap & rewrites)
{
    for (auto & rewrite : rewrites) {
        debug("rewriting %s as %s", rewrite.first, rewrite.second);
    }

    drv.builder = rewriteStrings(drv.builder, rewrites);
    for (auto & arg : drv.args) {
        arg = rewriteStrings(arg, rewrites);
    }

    StringPairs newEnv;
    for (auto & envVar : drv.env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second, rewrites);
        newEnv.emplace(envName, envValue);
    }
    drv.env = newEnv;

    auto hashModulo = hashDerivationModulo(store, Derivation(drv), true);
    for (auto & [outputName, output] : drv.outputs) {
        if (std::holds_alternative<DerivationOutput::Deferred>(output.raw())) {
            auto & h = hashModulo.hashes.at(outputName);
            auto outPath = store.makeOutputPath(outputName, h, drv.name);
            drv.env[outputName] = store.printStorePath(outPath);
            output = DerivationOutput::InputAddressed {
                .path = std::move(outPath),
            };
        }
    }

}

std::optional<BasicDerivation> Derivation::tryResolve(Store & store) const
{
    std::map<std::pair<StorePath, std::string>, StorePath> inputDrvOutputs;

    for (auto & input : inputDrvs)
        for (auto & [outputName, outputPath] : store.queryPartialDerivationOutputMap(input.first))
            if (outputPath)
                inputDrvOutputs.insert_or_assign({input.first, outputName}, *outputPath);

    return tryResolve(store, inputDrvOutputs);
}

std::optional<BasicDerivation> Derivation::tryResolve(
    Store & store,
    const std::map<std::pair<StorePath, std::string>, StorePath> & inputDrvOutputs) const
{
    BasicDerivation resolved { *this };

    // Input paths that we'll want to rewrite in the derivation
    StringMap inputRewrites;

    for (auto & [inputDrv, inputOutputs] : inputDrvs) {
        for (auto & outputName : inputOutputs) {
            if (auto actualPath = get(inputDrvOutputs, { inputDrv, outputName })) {
                inputRewrites.emplace(
                    downstreamPlaceholder(store, inputDrv, outputName),
                    store.printStorePath(*actualPath));
                resolved.inputSrcs.insert(*actualPath);
            } else {
                warn("output '%s' of input '%s' missing, aborting the resolving",
                    outputName,
                    store.printStorePath(inputDrv));
                return {};
            }
        }
    }

    rewriteDerivation(store, resolved, inputRewrites);

    return resolved;
}

const Hash impureOutputHash = hashString(htSHA256, "impure");

}
