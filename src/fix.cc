#include <iostream>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern "C" {
#include <aterm2.h>
}

#include "util.hh"


static string nixDescriptorDir;


static bool verbose = false;


/* Mapping of Fix file names to the hashes of the resulting Nix
   descriptors. */
typedef map<string, string> DescriptorMap;


void registerFile(string filename)
{
    int res = system(("nix regfile " + filename).c_str()); 
    /* !!! escape */
    if (WEXITSTATUS(res) != 0)
        throw Error("cannot register " + filename + " with Nix");
}


void registerURL(string hash, string url)
{
    int res = system(("nix regurl " + hash + " " + url).c_str());
    /* !!! escape */
    if (WEXITSTATUS(res) != 0)
        throw Error("cannot register " + hash + " -> " + url + " with Nix");
}


Error badTerm(const string & msg, ATerm e)
{
    char * s = ATwriteToString(e);
    return Error(msg + ", in `" + s + "'");
}


/* Term evaluation. */

typedef map<string, ATerm> BindingsMap;

struct EvalContext
{
    string dir;
    DescriptorMap * done;
    BindingsMap * vars;
};


ATerm evaluate(ATerm e, EvalContext ctx);
string instantiateDescriptor(string filename, EvalContext ctx);


string evaluateStr(ATerm e, EvalContext ctx)
{
    e = evaluate(e, ctx);
    char * s;
    if (ATmatch(e, "Str(<str>)", &s))
        return s;
    else throw badTerm("string value expected", e);
}


bool evaluateBool(ATerm e, EvalContext ctx)
{
    e = evaluate(e, ctx);
    if (ATmatch(e, "Bool(True)"))
        return true;
    else if (ATmatch(e, "Bool(False)"))
        return false;
    else throw badTerm("boolean value expected", e);
}


ATerm evaluate(ATerm e, EvalContext ctx)
{
    char * s;
    ATerm e2, e3;
    ATerm eCond, eTrue, eFalse;

    /* Check for normal forms first. */

    if (ATmatch(e, "Str(<str>)", &s) ||
        ATmatch(e, "Bool(True)") || ATmatch(e, "Bool(False)"))
        return e;
    
    else if (
        ATmatch(e, "Pkg(<str>)", &s) || 
        ATmatch(e, "File(<str>)", &s))
    {
        checkHash(s);
        return e;
    }

    /* Short-hands. */

    else if (ATmatch(e, "<str>", &s))
        return ATmake("Str(<str>)", s);

    else if (ATmatch(e, "True", &s))
        return ATmake("Bool(True)", s);

    else if (ATmatch(e, "False", &s))
        return ATmake("Bool(False)", s);

    /* Functions. */

    /* `Var' looks up a variable. */
    else if (ATmatch(e, "Var(<str>)", &s)) {
        string name(s);
        ATerm e2 = (*ctx.vars)[name];
        if (!e2) throw Error("undefined variable " + name);
        return evaluate(e2, ctx); /* !!! update binding */
    }

    /* `Fix' recursively instantiates a Fix descriptor, returning the
       hash of the generated Nix descriptor. */
    else if (ATmatch(e, "Fix(<term>)", &e2)) {
        string filename = absPath(evaluateStr(e2, ctx), ctx.dir); /* !!! */
        return ATmake("Pkg(<str>)",
            instantiateDescriptor(filename, ctx).c_str());
    }

#if 0
    /* `Source' copies the specified file to nixSourcesDir, registers
       it with Nix, and returns the hash of the file. */
    else if (ATmatch(e, "Source(<term>)", &e2)) {
        string source = absPath(evaluateStr(e2, ctx), ctx.dir); /* !!! */
        string target = nixSourcesDir + "/" + baseNameOf(source);

        // Don't copy if filename is already in nixSourcesDir.
        if (source != target) {
            if (verbose)
                cerr << "copying source " << source << endl;
            string cmd = "cp -p " + source + " " + target;
            int res = system(cmd.c_str());
            if (WEXITSTATUS(res) != 0)
                throw Error("cannot copy " + source + " to " + target);
        }
        
        registerFile(target);
        return ATmake("File(<str>)", hashFile(target).c_str());
    }
#endif

    /* `Local' registers a file with Nix, and returns the file's
       hash. */
    else if (ATmatch(e, "Local(<term>)", &e2)) {
        string filename = absPath(evaluateStr(e2, ctx), ctx.dir); /* !!! */
        string hash = hashFile(filename);
        registerFile(filename); /* !!! */
        return ATmake("File(<str>)", hash.c_str());
    }

    /* `Url' registers a mapping from a hash to an url with Nix, and
       returns the hash. */
    else if (ATmatch(e, "Url(<term>, <term>)", &e2, &e3)) {
        string hash = evaluateStr(e2, ctx);
        checkHash(hash);
        string url = evaluateStr(e3, ctx);
        registerURL(hash, url);
        return ATmake("File(<str>)", hash.c_str());
    }

    /* `If' provides conditional evaluation. */
    else if (ATmatch(e, "If(<term>, <term>, <term>)", 
                 &eCond, &eTrue, &eFalse)) 
        return evaluate(evaluateBool(eCond, ctx) ? eTrue : eFalse, ctx);

    else throw badTerm("invalid expression", e);
}


string getStringFromMap(BindingsMap & bindingsMap,
    const string & name)
{
    ATerm e = bindingsMap[name];
    if (!e) throw Error("binding " + name + " is not set");
    char * s;
    if (ATmatch(e, "Str(<str>)", &s))
        return s;
    else
        throw Error("binding " + name + " is not a string");
}


/* Instantiate a Fix descriptors into a Nix descriptor, recursively
   instantiating referenced descriptors as well. */
string instantiateDescriptor(string filename, EvalContext ctx)
{
    /* Already done? */
    DescriptorMap::iterator isInMap = ctx.done->find(filename);
    if (isInMap != ctx.done->end()) return isInMap->second;

    /* No. */
    ctx.dir = dirOf(filename);

    /* Read the Fix descriptor as an ATerm. */
    ATerm inTerm = ATreadFromNamedFile(filename.c_str());
    if (!inTerm) throw Error("cannot read aterm " + filename);

    ATerm bindings;
    if (!ATmatch(inTerm, "Descr(<term>)", &bindings))
        throw Error("invalid term in " + filename);
    
    /* Iterate over the bindings and evaluate them to normal form. */
    BindingsMap bindingsMap; /* the normal forms */
    ctx.vars = &bindingsMap;

    char * cname;
    ATerm value;
    while (ATmatch(bindings, "[Bind(<str>, <term>), <list>]", 
               &cname, &value, &bindings)) 
    {
        string name(cname);
        ATerm e = evaluate(value, ctx);
        bindingsMap[name] = e;
    }

    /* Construct a descriptor identifier by concatenating the package
       and release ids. */
    string pkgId = getStringFromMap(bindingsMap, "pkgId");
    string releaseId = getStringFromMap(bindingsMap, "releaseId");
    string id = pkgId + "-" + releaseId;
    bindingsMap["id"] = ATmake("Str(<str>)", id.c_str());

    /* Add a system name. */
    bindingsMap["system"] = ATmake("Str(<str>)", thisSystem.c_str());
         
    /* Construct the resulting ATerm.  Note that iterating over the
       map yields the bindings in sorted order, which is exactly the
       canonical form for Nix descriptors. */
    ATermList bindingsList = ATempty;
    for (BindingsMap::iterator it = bindingsMap.begin();
         it != bindingsMap.end(); it++)
        /* !!! O(n^2) */
        bindingsList = ATappend(bindingsList,
            ATmake("Bind(<str>, <term>)", it->first.c_str(), it->second));
    ATerm outTerm = ATmake("Descr(<term>)", bindingsList);

    /* Write out the resulting ATerm. */
    string tmpFilename = nixDescriptorDir + "/tmp";
    if (!ATwriteToNamedTextFile(outTerm, tmpFilename.c_str()))
        throw Error("cannot write aterm to " + tmpFilename);

    string outHash = hashFile(tmpFilename);
    string outFilename = nixDescriptorDir + "/" + id + "-" + outHash + ".nix";
    if (rename(tmpFilename.c_str(), outFilename.c_str()))
        throw Error("cannot rename " + tmpFilename + " to " + outFilename);

    /* Register it with Nix. */
    registerFile(outFilename);

    if (verbose)
        cerr << "instantiated " << outHash << " from " << filename << endl;

    (*ctx.done)[filename] = outHash;
    return outHash;
}


/* Instantiate a set of Fix descriptors into Nix descriptors. */
void instantiateDescriptors(Strings filenames)
{
    DescriptorMap done;

    EvalContext ctx;
    ctx.done = &done;

    for (Strings::iterator it = filenames.begin();
         it != filenames.end(); it++)
    {
        string filename = absPath(*it);
        cout << instantiateDescriptor(filename, ctx) << endl;
    }
}


/* Print help. */
void printUsage()
{
    cerr <<
"Usage: fix ...
";
}


/* Parse the command-line arguments, call the right operation. */
void run(Strings::iterator argCur, Strings::iterator argEnd)
{
    umask(0022);

    Strings extraArgs;
    enum { cmdUnknown, cmdInstantiate } command = cmdUnknown;

    char * homeDir = getenv(nixHomeDirEnvVar.c_str());
    if (homeDir) nixHomeDir = homeDir;

    nixDescriptorDir = nixHomeDir + "/var/nix/descriptors";

    for ( ; argCur != argEnd; argCur++) {
        string arg(*argCur);
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--instantiate" || arg == "-i") {
            command = cmdInstantiate;
        } else if (arg[0] == '-')
            throw UsageError("invalid option `" + arg + "'");
        else
            extraArgs.push_back(arg);
    }

    switch (command) {

        case cmdInstantiate:
            instantiateDescriptors(extraArgs);
            break;

        default:
            throw UsageError("no operation specified");
    }
}


int main(int argc, char * * argv)
{
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Put the arguments in a vector. */
    Strings args;
    while (argc--) args.push_back(*argv++);
    Strings::iterator argCur = args.begin(), argEnd = args.end();

    argCur++;

    try {
        run(argCur, argEnd);
    } catch (UsageError & e) {
        cerr << "error: " << e.what() << endl
             << "Try `fix -h' for more information.\n";
        return 1;
    } catch (exception & e) {
        cerr << "error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
