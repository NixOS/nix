#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "misc.hh"
#include "worker-protocol.hh"


namespace nix {


void DerivationOutput::parseHashInfo(bool & recursive, HashType & hashType, Hash & hash) const
{
    recursive = false;
    string algo = hashAlgo;

    if (string(algo, 0, 2) == "r:") {
        recursive = true;
        algo = string(algo, 2);
    }

    hashType = parseHashType(algo);
    if (hashType == htUnknown)
        throw Error(format("unknown hash algorithm ‘%1%’") % algo);

    hash = parseHash(hashType, this->hash);
}


Path writeDerivation(StoreAPI & store,
    const Derivation & drv, const string & name, bool repair)
{
    PathSet references;
    references.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
    for (auto & i : drv.inputDrvs)
        references.insert(i.first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    string suffix = name + drvExtension;
    string contents = unparseDerivation(drv);
    return settings.readOnlyMode
        ? computeStorePathForText(suffix, contents, references)
        : store.addTextToStore(suffix, contents, references, repair);
}


static Path parsePath(std::istream & str)
{
    string s = parseString(str);
    if (s.size() == 0 || s[0] != '/')
        throw FormatError(format("bad path ‘%1%’ in derivation") % s);
    return s;
}


static StringSet parseStrings(std::istream & str, bool arePaths)
{
    StringSet res;
    while (!endOfList(str))
        res.insert(arePaths ? parsePath(str) : parseString(str));
    return res;
}


static Derivation parseDerivation(const string & s)
{
    Derivation drv;
    std::istringstream str(s);
    expect(str, "Derive([");

    /* Parse the list of outputs. */
    while (!endOfList(str)) {
        DerivationOutput out;
        expect(str, "("); string id = parseString(str);
        expect(str, ","); out.path = parsePath(str);
        expect(str, ","); out.hashAlgo = parseString(str);
        expect(str, ","); out.hash = parseString(str);
        expect(str, ")");
        drv.outputs[id] = out;
    }

    /* Parse the list of input derivations. */
    expect(str, ",[");
    while (!endOfList(str)) {
        expect(str, "(");
        Path drvPath = parsePath(str);
        expect(str, ",[");
        drv.inputDrvs[drvPath] = parseStrings(str, false);
        expect(str, ")");
    }

    expect(str, ",["); drv.inputSrcs = parseStrings(str, true);
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


Derivation readDerivation(const Path & drvPath)
{
    try {
        return parseDerivation(readFile(drvPath));
    } catch (FormatError & e) {
        throw Error(format("error parsing derivation ‘%1%’: %2%") % drvPath % e.msg());
    }
}


static void printString(string & res, const string & s)
{
    res += '"';
    for (const char * i = s.c_str(); *i; i++)
        if (*i == '\"' || *i == '\\') { res += "\\"; res += *i; }
        else if (*i == '\n') res += "\\n";
        else if (*i == '\r') res += "\\r";
        else if (*i == '\t') res += "\\t";
        else res += *i;
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


string unparseDerivation(const Derivation & drv)
{
    string s;
    s.reserve(65536);
    s += "Derive([";

    bool first = true;
    for (auto & i : drv.outputs) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printString(s, i.second.path);
        s += ','; printString(s, i.second.hashAlgo);
        s += ','; printString(s, i.second.hash);
        s += ')';
    }

    s += "],[";
    first = true;
    for (auto & i : drv.inputDrvs) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printStrings(s, i.second.begin(), i.second.end());
        s += ')';
    }

    s += "],";
    printStrings(s, drv.inputSrcs.begin(), drv.inputSrcs.end());

    s += ','; printString(s, drv.platform);
    s += ','; printString(s, drv.builder);
    s += ','; printStrings(s, drv.args.begin(), drv.args.end());

    s += ",[";
    first = true;
    for (auto & i : drv.env) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printString(s, i.second);
        s += ')';
    }

    s += "])";

    return s;
}


bool isDerivation(const string & fileName)
{
    return hasSuffix(fileName, drvExtension);
}


bool isFixedOutputDrv(const Derivation & drv)
{
    return drv.outputs.size() == 1 &&
        drv.outputs.begin()->first == "out" &&
        drv.outputs.begin()->second.hash != "";
}


DrvHashes drvHashes;


/* Returns the hash of a derivation modulo fixed-output
   subderivations.  A fixed-output derivation is a derivation with one
   output (`out') for which an expected hash and hash algorithm are
   specified (using the `outputHash' and `outputHashAlgo'
   attributes).  We don't want changes to such derivations to
   propagate upwards through the dependency graph, changing output
   paths everywhere.

   For instance, if we change the url in a call to the `fetchurl'
   function, we do not want to rebuild everything depending on it
   (after all, (the hash of) the file being downloaded is unchanged).
   So the *output paths* should not change.  On the other hand, the
   *derivation paths* should change to reflect the new dependency
   graph.

   That's what this function does: it returns a hash which is just the
   hash of the derivation ATerm, except that any input derivation
   paths have been replaced by the result of a recursive call to this
   function, and that for fixed-output derivations we return a hash of
   its output path. */
Hash hashDerivationModulo(StoreAPI & store, Derivation drv)
{
    /* Return a fixed hash for fixed-output derivations. */
    if (isFixedOutputDrv(drv)) {
        DerivationOutputs::const_iterator i = drv.outputs.begin();
        return hashString(htSHA256, "fixed:out:"
            + i->second.hashAlgo + ":"
            + i->second.hash + ":"
            + i->second.path);
    }

    /* For other derivations, replace the inputs paths with recursive
       calls to this function.*/
    DerivationInputs inputs2;
    for (auto & i : drv.inputDrvs) {
        Hash h = drvHashes[i.first];
        if (h.type == htUnknown) {
            assert(store.isValidPath(i.first));
            Derivation drv2 = readDerivation(i.first);
            h = hashDerivationModulo(store, drv2);
            drvHashes[i.first] = h;
        }
        inputs2[printHash(h)] = i.second;
    }
    drv.inputDrvs = inputs2;

    return hashString(htSHA256, unparseDerivation(drv));
}


DrvPathWithOutputs parseDrvPathWithOutputs(const string & s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? DrvPathWithOutputs(s, std::set<string>())
        : DrvPathWithOutputs(string(s, 0, n), tokenizeString<std::set<string> >(string(s, n + 1), ","));
}


Path makeDrvPathWithOutputs(const Path & drvPath, const std::set<string> & outputs)
{
    return outputs.empty()
        ? drvPath
        : drvPath + "!" + concatStringsSep(",", outputs);
}


bool wantOutput(const string & output, const std::set<string> & wanted)
{
    return wanted.empty() || wanted.find(output) != wanted.end();
}


PathSet outputPaths(const BasicDerivation & drv)
{
    PathSet paths;
    for (auto & i : drv.outputs)
        paths.insert(i.second.path);
    return paths;
}


Source & operator >> (Source & in, BasicDerivation & drv)
{
    drv.outputs.clear();
    auto nr = readInt(in);
    for (unsigned int n = 0; n < nr; n++) {
        auto name = readString(in);
        DerivationOutput o;
        in >> o.path >> o.hashAlgo >> o.hash;
        assertStorePath(o.path);
        drv.outputs[name] = o;
    }

    drv.inputSrcs = readStorePaths<PathSet>(in);
    in >> drv.platform >> drv.builder;
    drv.args = readStrings<Strings>(in);

    nr = readInt(in);
    for (unsigned int n = 0; n < nr; n++) {
        auto key = readString(in);
        auto value = readString(in);
        drv.env[key] = value;
    }

    return in;
}


Sink & operator << (Sink & out, const BasicDerivation & drv)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs)
        out << i.first << i.second.path << i.second.hashAlgo << i.second.hash;
    out << drv.inputSrcs << drv.platform << drv.builder << drv.args;
    out << drv.env.size();
    for (auto & i : drv.env)
        out << i.first << i.second;
    return out;
}


}
