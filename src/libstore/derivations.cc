#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"


namespace nix {


Path writeDerivation(const Derivation & drv, const string & name)
{
    PathSet references;
    references.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
    foreach (DerivationInputs::const_iterator, i, drv.inputDrvs)
        references.insert(i->first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    string suffix = name + drvExtension;
    string contents = unparseDerivation(drv);
    return readOnlyMode
        ? computeStorePathForText(suffix, contents, references)
        : store->addTextToStore(suffix, contents, references);
}


static Path parsePath(std::istream & str)
{
    string s = parseString(str);
    if (s.size() == 0 || s[0] != '/')
        throw Error(format("bad path `%1%' in derivation") % s);
    return s;
}
    

static StringSet parseStrings(std::istream & str, bool arePaths)
{
    StringSet res;
    while (!endOfList(str))
        res.insert(arePaths ? parsePath(str) : parseString(str));
    return res;
}
    

Derivation parseDerivation(const string & s)
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


void printString(std::ostream & str, const string & s)
{
    str << "\"";
    for (const char * i = s.c_str(); *i; i++)
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else str << *i;
    str << "\"";
}


template<class ForwardIterator>
void printStrings(std::ostream & str, ForwardIterator i, ForwardIterator j)
{
    str << "[";
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else str << ",";
        printString(str, *i);
    }
    str << "]";
}


string unparseDerivation(const Derivation & drv)
{
    std::ostringstream str;
    str << "Derive([";

    bool first = true;
    foreach (DerivationOutputs::const_iterator, i, drv.outputs) {
        if (first) first = false; else str << ",";
        str << "("; printString(str, i->first);
        str << ","; printString(str, i->second.path);
        str << ","; printString(str, i->second.hashAlgo);
        str << ","; printString(str, i->second.hash);
        str << ")";
    }

    str << "],[";
    first = true;
    foreach (DerivationInputs::const_iterator, i, drv.inputDrvs) {
        if (first) first = false; else str << ",";
        str << "("; printString(str, i->first);
        str << ","; printStrings(str, i->second.begin(), i->second.end());
        str << ")";
    }

    str << "],";
    printStrings(str, drv.inputSrcs.begin(), drv.inputSrcs.end());
    
    str << ","; printString(str, drv.platform);
    str << ","; printString(str, drv.builder);
    str << ","; printStrings(str, drv.args.begin(), drv.args.end());

    str << ",[";
    first = true;
    foreach (StringPairs::const_iterator, i, drv.env) {
        if (first) first = false; else str << ",";
        str << "("; printString(str, i->first);
        str << ","; printString(str, i->second);
        str << ")";
    }
    
    str << "])";
    
    return str.str();
}


bool isDerivation(const string & fileName)
{
    return hasSuffix(fileName, drvExtension);
}

 
}
