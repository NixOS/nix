#include <iostream>
#include <map>

extern "C" {
#include <aterm2.h>
}

#include "util.hh"


static string nixDescriptorDir;
static string nixSourcesDir;


typedef map<string, string> DescriptorMap;


void registerFile(string filename)
{
    int res = system(("nix regfile " + filename).c_str());
    if (WEXITSTATUS(res) != 0)
        throw Error("cannot register " + filename + " with Nix");
}


/* Download object referenced by the given URL into the sources
   directory.  Return the file name it was downloaded to. */
string fetchURL(string url)
{
    unsigned int pos = url.rfind('/');
    if (pos == string::npos) throw Error("invalid url");
    string filename(url, pos + 1);
    string fullname = nixSourcesDir + "/" + filename;
    /* !!! quoting */
    string shellCmd =
        "cd " + nixSourcesDir + " && wget --quiet -N \"" + url + "\"";
    int res = system(shellCmd.c_str());
    if (WEXITSTATUS(res) != 0)
        throw Error("cannot fetch " + url);
    return fullname;
}


/* Return the directory part of the given path, i.e., everything
   before the final `/'. */
string dirOf(string s)
{
    unsigned int pos = s.rfind('/');
    if (pos == string::npos) throw Error("invalid file name");
    return string(s, 0, pos);
}


/* Term evaluation functions. */

string evaluateStr(ATerm e)
{
    char * s;
    if (ATmatch(e, "<str>", &s))
        return s;
    else throw Error("invalid string expression");
}


ATerm evaluateBool(ATerm e)
{
    if (ATmatch(e, "True") || ATmatch(e, "False"))
        return e;
    else throw Error("invalid boolean expression");
}


string evaluateFile(ATerm e, string dir)
{
    char * s;
    ATerm t;
    if (ATmatch(e, "<str>", &s)) {
        checkHash(s);
        return s;
    } else if (ATmatch(e, "Url(<term>)", &t)) {
        string url = evaluateStr(t);
        string filename = fetchURL(url);
        registerFile(filename);
        return hashFile(filename);
    } else if (ATmatch(e, "Local(<term>)", &t)) {
        string filename = absPath(evaluateStr(t), dir); /* !!! */
        string cmd = "cp -p " + filename + " " + nixSourcesDir;
        int res = system(cmd.c_str());
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot copy " + filename);
        return hashFile(filename);
    } else throw Error("invalid hash expression");
}


ATerm evaluatePkg(ATerm e, DescriptorMap & done)
{
    char * s;
    if (ATmatch(e, "<str>", &s)) {
        checkHash(s);
        return s;
    } else throw Error("invalid hash expression");
}


ATerm evaluate(ATerm e, string dir, DescriptorMap & done)
{
    ATerm t;
    if (ATmatch(e, "Str(<term>)", &t))
        return ATmake("Str(<str>)", evaluateStr(t).c_str());
    else if (ATmatch(e, "Bool(<term>)", &t))
        return ATmake("Bool(<term>)", evaluateBool(t));
    else if (ATmatch(e, "File(<term>)", &t))
        return ATmake("File(<str>)", evaluateFile(t, dir).c_str());
    else if (ATmatch(e, "Pkg(<term>)", &t))
        return ATmake("Pkg(<term>)", evaluatePkg(t, done));
    else throw Error("invalid expression type");
}


typedef map<string, ATerm> BindingsMap;


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
string instantiateDescriptor(string filename,
    DescriptorMap & done)
{
    /* Already done? */
    DescriptorMap::iterator isInMap = done.find(filename);
    if (isInMap != done.end()) return isInMap->second;

    /* No. */
    string dir = dirOf(filename);

    /* Read the Fix descriptor as an ATerm. */
    ATerm inTerm = ATreadFromNamedFile(filename.c_str());
    if (!inTerm) throw Error("cannot read aterm " + filename);

    ATerm bindings;
    if (!ATmatch(inTerm, "Descr(<term>)", &bindings))
        throw Error("invalid term in " + filename);
    
    /* Iterate over the bindings and evaluate them to normal form. */
    BindingsMap bindingsMap; /* the normal forms */

    char * cname;
    ATerm value;
    while (ATmatch(bindings, "[Bind(<str>, <term>), <list>]", 
               &cname, &value, &bindings)) 
    {
        string name(cname);
        ATerm e = evaluate(value, dir, done);
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

    cout << outFilename << endl;

    /* Register it with Nix. */
    registerFile(outFilename);

    done[filename] = outFilename;
    return outFilename;
}


/* Instantiate a set of Fix descriptors into Nix descriptors. */
void instantiateDescriptors(Strings filenames)
{
    DescriptorMap done;

    for (Strings::iterator it = filenames.begin();
         it != filenames.end(); it++)
    {
        string filename = absPath(*it);
        instantiateDescriptor(filename, done);
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
    Strings extraArgs;
    enum { cmdUnknown, cmdInstantiate } command = cmdUnknown;

    char * homeDir = getenv(nixHomeDirEnvVar.c_str());
    if (homeDir) nixHomeDir = homeDir;

    nixDescriptorDir = nixHomeDir + "/var/nix/descriptors";
    nixSourcesDir = nixHomeDir + "/var/nix/sources";

    for ( ; argCur != argEnd; argCur++) {
        string arg(*argCur);
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return;
        } if (arg == "--instantiate" || arg == "-i") {
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
