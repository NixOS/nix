#include <iostream>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

extern "C" {
#include <aterm1.h>
}

#include "util.hh"
#include "db.hh"

using namespace std;


/* Database names. */
static string dbRefs = "refs";
static string dbInstPkgs = "pkginst";
static string dbPrebuilts = "prebuilts";
static string dbNetSources = "netsources";


static string nixSourcesDir;
static string nixDB;


/* Download object referenced by the given URL into the sources
   directory.  Return the file name it was downloaded to. */
string fetchURL(string url)
{
    string filename = baseNameOf(url);
    string fullname = nixSourcesDir + "/" + filename;
    struct stat st;
    if (stat(fullname.c_str(), &st)) {
        cerr << "fetching " << url << endl;
        /* !!! quoting */
        string shellCmd =
            "cd " + nixSourcesDir + " && wget --quiet -N \"" + url + "\"";
        int res = system(shellCmd.c_str());
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot fetch " + url);
    }
    return fullname;
}


/* Obtain an object with the given hash.  If a file with that hash is
   known to exist in the local file system (as indicated by the dbRefs
   database), we use that.  Otherwise, we attempt to fetch it from the
   network (using dbNetSources).  We verify that the file has the
   right hash. */
string getFile(string hash)
{
    bool checkedNet = false;

    while (1) {

        string fn, url;

        if (queryDB(nixDB, dbRefs, hash, fn)) {

            /* Verify that the file hasn't changed. !!! race */
            if (hashFile(fn) != hash)
                throw Error("file " + fn + " is stale");

            return fn;
        }

        if (checkedNet)
            throw Error("consistency problem: file fetched from " + url + 
                " should have hash " + hash + ", but it doesn't");

        if (!queryDB(nixDB, dbNetSources, hash, url))
            throw Error("a file with hash " + hash + " is requested, "
                "but it is not known to exist locally or on the network");

        checkedNet = true;
        
        fn = fetchURL(url);
        
        setDB(nixDB, dbRefs, hash, fn);
    }
}


typedef map<string, string> Params;


void readPkgDescr(const string & hash,
    Params & pkgImports, Params & fileImports, Params & arguments)
{
    string pkgfile;

    pkgfile = getFile(hash);

    ATerm term = ATreadFromNamedFile(pkgfile.c_str());
    if (!term) throw Error("cannot read aterm " + pkgfile);

    ATerm bindings;
    if (!ATmatch(term, "Descr(<term>)", &bindings))
        throw Error("invalid term in " + pkgfile);

    char * cname;
    ATerm value;
    while (ATmatch(bindings, "[Bind(<str>, <term>), <list>]", 
               &cname, &value, &bindings)) 
    {
        string name(cname);
        char * arg;
        if (ATmatch(value, "Pkg(<str>)", &arg)) {
            checkHash(arg);
            pkgImports[name] = arg;
        } else if (ATmatch(value, "File(<str>)", &arg)) {
            checkHash(arg);
            fileImports[name] = arg;
        } else if (ATmatch(value, "Str(<str>)", &arg))
            arguments[name] = arg;
        else if (ATmatch(value, "Bool(True)"))
            arguments[name] = "1";
        else if (ATmatch(value, "Bool(False)"))
            arguments[name] = "";
        else {
            ATprintf("%t\n", value);
            throw Error("invalid binding in " + pkgfile);
        }
    }
}


string getPkg(string hash);


typedef map<string, string> Environment;


void fetchDeps(string hash, Environment & env)
{
    /* Read the package description file. */
    Params pkgImports, fileImports, arguments;
    readPkgDescr(hash, pkgImports, fileImports, arguments);

    /* Recursively fetch all the dependencies, filling in the
       environment as we go along. */
    for (Params::iterator it = pkgImports.begin();
         it != pkgImports.end(); it++)
    {
        cerr << "fetching package dependency "
             << it->first << " <- " << it->second
             << endl;
        env[it->first] = getPkg(it->second);
    }

    for (Params::iterator it = fileImports.begin();
         it != fileImports.end(); it++)
    {
        cerr << "fetching file dependency "
             << it->first << " = " << it->second
             << endl;

        string file;

        file = getFile(it->second);

        env[it->first] = file;
    }

    string buildSystem;

    for (Params::iterator it = arguments.begin();
         it != arguments.end(); it++)
    {
        env[it->first] = it->second;
        if (it->first == "system")
            buildSystem = it->second;
    }

    if (buildSystem != thisSystem)
        throw Error("descriptor requires a `" + buildSystem +
            "' but I am a `" + thisSystem + "'");
}


string getFromEnv(const Environment & env, const string & key)
{
    Environment::const_iterator it = env.find(key);
    if (it == env.end())
        throw Error("key " + key + " not found in the environment");
    return it->second;
}


string queryPkgId(const string & hash)
{
    Params pkgImports, fileImports, arguments;
    readPkgDescr(hash, pkgImports, fileImports, arguments);
    return getFromEnv(arguments, "id");
}


void installPkg(string hash)
{
    string pkgfile;
    string src;
    string path;
    string cmd;
    string builder;
    Environment env;

    /* Fetch dependencies. */
    fetchDeps(hash, env);

    builder = getFromEnv(env, "build");

    string id = getFromEnv(env, "id");

    /* Construct a path for the installed package. */
    path = nixHomeDir + "/pkg/" + id + "-" + hash;

    /* Create the path. */
    if (mkdir(path.c_str(), 0777))
        throw Error("unable to create directory " + path);

    try {

        /* Fork a child to build the package. */
        pid_t pid;
        switch (pid = fork()) {
            
        case -1:
            throw Error("unable to fork");

        case 0: { /* child */

            /* Go to the build directory. */
            if (chdir(path.c_str())) {
                cerr << "unable to chdir to package directory\n";
                _exit(1);
            }

            /* Try to use a prebuilt. */
            string prebuiltHash, prebuiltFile;
            if (queryDB(nixDB, dbPrebuilts, hash, prebuiltHash)) {

                try {
                    prebuiltFile = getFile(prebuiltHash);
                } catch (Error e) {
                    cerr << "cannot obtain prebuilt (ignoring): " << e.what() << endl;
                    goto build;
                }
                
                cerr << "substituting prebuilt " << prebuiltFile << endl;

                int res = system(("tar xfj " + prebuiltFile + " 1>&2").c_str()); // !!! escaping
                if (WEXITSTATUS(res) != 0)
                    /* This is a fatal error, because path may now
                       have clobbered. */
                    throw Error("cannot unpack " + prebuiltFile);

                _exit(0);
            }

build:

            /* Fill in the environment.  We don't bother freeing the
               strings, since we'll exec or die soon anyway. */
            const char * env2[env.size() + 1];
            int i = 0;
            for (Environment::iterator it = env.begin();
                 it != env.end(); it++, i++)
                env2[i] = (new string(it->first + "=" + it->second))->c_str();
            env2[i] = 0;

	    /* Dup stderr to stdin. */
	    dup2(STDERR_FILENO, STDOUT_FILENO);

            /* Execute the builder.  This should not return. */
            execle(builder.c_str(), builder.c_str(), 0, env2);

            cerr << strerror(errno) << endl;

            cerr << "unable to execute builder\n";
            _exit(1); }

        }

        /* parent */

        /* Wait for the child to finish. */
        int status;
        if (waitpid(pid, &status, 0) != pid)
            throw Error("unable to wait for child");
    
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw Error("unable to build package");

        /* Remove write permission from the build directory. */
        int res = system(("chmod -R -w " + path).c_str()); // !!! escaping
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot remove write permission from " + path);
    
    } catch (exception &) {
        system(("rm -rf " + path).c_str());
        throw;
    }

    setDB(nixDB, dbInstPkgs, hash, path);
}


string getPkg(string hash)
{
    string path;
    checkHash(hash);
    while (!queryDB(nixDB, dbInstPkgs, hash, path))
        installPkg(hash);
    return path;
}


void runPkg(string hash, 
    Strings::iterator firstArg, 
    Strings::iterator lastArg)
{
    string src;
    string path;
    string cmd;
    string runner;
    Environment env;

    /* Fetch dependencies. */
    fetchDeps(hash, env);

    runner = getFromEnv(env, "run");
    
    /* Fill in the environment.  We don't bother freeing the
       strings, since we'll exec or die soon anyway. */
    for (Environment::iterator it = env.begin();
         it != env.end(); it++)
    {
        string * s = new string(it->first + "=" + it->second);
        putenv((char *) s->c_str());
    }

    /* Create the list of arguments. */
    const char * args2[env.size() + 2];
    int i = 0;
    args2[i++] = runner.c_str();
    for (Strings::const_iterator it = firstArg; it != lastArg; it++, i++)
        args2[i] = it->c_str();
    args2[i] = 0;

    /* Execute the runner.  This should not return. */
    execv(runner.c_str(), (char * *) args2);

    cerr << strerror(errno) << endl;
    throw Error("unable to execute runner");
}


void ensurePkg(string hash)
{
    Params pkgImports, fileImports, arguments;
    readPkgDescr(hash, pkgImports, fileImports, arguments);

    if (fileImports.find("build") != fileImports.end())
        getPkg(hash);
    else if (fileImports.find("run") != fileImports.end()) {
        Environment env;
        fetchDeps(hash, env);
    } else throw Error("invalid descriptor");
}


void delPkg(string hash)
{
    string path;
    checkHash(hash);
    if (queryDB(nixDB, dbInstPkgs, hash, path)) {
        int res = system(("chmod -R +w " + path + " && rm -rf " + path).c_str()); // !!! escaping
        delDB(nixDB, dbInstPkgs, hash); // not a bug ??? 
        if (WEXITSTATUS(res) != 0)
            cerr << "errors deleting " + path + ", ignoring" << endl;
    }
}


void exportPkgs(string outDir, 
    Strings::iterator firstHash, 
    Strings::iterator lastHash)
{
    outDir = absPath(outDir);

    for (Strings::iterator it = firstHash; it != lastHash; it++) {
        string hash = *it;
        string pkgDir = getPkg(hash);
        string tmpFile = outDir + "/export_tmp";

        string cmd = "cd " + pkgDir + " && tar cfj " + tmpFile + " .";
        int res = system(cmd.c_str()); // !!! escaping
        if (!WIFEXITED(res) || WEXITSTATUS(res) != 0)
            throw Error("cannot tar " + pkgDir);

        string prebuiltHash = hashFile(tmpFile);
        string pkgId = queryPkgId(hash);
        string prebuiltFile = outDir + "/" +
            pkgId + "-" + hash + "-" + prebuiltHash + ".tar.bz2";
        
        rename(tmpFile.c_str(), prebuiltFile.c_str());
    }
}


void registerPrebuilt(string pkgHash, string prebuiltHash)
{
    checkHash(pkgHash);
    checkHash(prebuiltHash);
    setDB(nixDB, dbPrebuilts, pkgHash, prebuiltHash);
}


string registerFile(string filename)
{
    filename = absPath(filename);
    string hash = hashFile(filename);
    setDB(nixDB, dbRefs, hash, filename);
    return hash;
}


void registerURL(string hash, string url)
{
    checkHash(hash);
    setDB(nixDB, dbNetSources, hash, url);
    /* !!! currently we allow only one network source per hash */
}


/* This is primarily used for bootstrapping. */
void registerInstalledPkg(string hash, string path)
{
    checkHash(hash);
    if (path == "")
        delDB(nixDB, dbInstPkgs, hash);
    else
        setDB(nixDB, dbInstPkgs, hash, path);
}


void initDB()
{
    createDB(nixDB, dbRefs);
    createDB(nixDB, dbInstPkgs);
    createDB(nixDB, dbPrebuilts);
    createDB(nixDB, dbNetSources);
}


void verifyDB()
{
    /* Check that all file references are still valid. */
    DBPairs fileRefs;
    
    enumDB(nixDB, dbRefs, fileRefs);

    for (DBPairs::iterator it = fileRefs.begin();
         it != fileRefs.end(); it++)
    {
        try {
            if (hashFile(it->second) != it->first) {
                cerr << "file " << it->second << " has changed\n";
                delDB(nixDB, dbRefs, it->first);
            }
        } catch (BadRefError e) { /* !!! better error check */ 
            cerr << "file " << it->second << " has disappeared\n";
            delDB(nixDB, dbRefs, it->first);
        }
    }

    /* Check that all installed packages are still there. */
    DBPairs instPkgs;

    enumDB(nixDB, dbInstPkgs, instPkgs);

    for (DBPairs::iterator it = instPkgs.begin();
         it != instPkgs.end(); it++)
    {
        struct stat st;
        if (stat(it->second.c_str(), &st) == -1) {
            cerr << "package " << it->first << " has disappeared\n";
            delDB(nixDB, dbInstPkgs, it->first);
        }
    }

    /* TODO: check that all directories in pkgHome are installed
       packages. */
}


void listInstalledPkgs()
{
    DBPairs instPkgs;

    enumDB(nixDB, dbInstPkgs, instPkgs);

    for (DBPairs::iterator it = instPkgs.begin();
         it != instPkgs.end(); it++)
        cout << it->first << endl;
}


void printInfo(Strings::iterator first, Strings::iterator last)
{
    for (Strings::iterator it = first; it != last; it++) {
        try {
            cout << *it << " " << queryPkgId(*it) << endl;
        } catch (Error & e) { // !!! more specific
            cout << *it << " (descriptor missing)\n";
        }
    }
}


void computeClosure(Strings::iterator first, Strings::iterator last, 
    set<string> & result)
{
    list<string> workList(first, last);
    set<string> doneSet;

    while (!workList.empty()) {
        string hash = workList.front();
        workList.pop_front();
        
        if (doneSet.find(hash) == doneSet.end()) {
            doneSet.insert(hash);
    
            Params pkgImports, fileImports, arguments;
            readPkgDescr(hash, pkgImports, fileImports, arguments);

            for (Params::iterator it = pkgImports.begin();
                 it != pkgImports.end(); it++)
                workList.push_back(it->second);
        }
    }

    result = doneSet;
}


void printClosure(Strings::iterator first, Strings::iterator last)
{
    set<string> allHashes;
    computeClosure(first, last, allHashes);
    for (set<string>::iterator it = allHashes.begin();
         it != allHashes.end(); it++)
        cout << *it << endl;
}


string dotQuote(const string & s)
{
    return "\"" + s + "\"";
}


void printGraph(Strings::iterator first, Strings::iterator last)
{
    set<string> allHashes;
    computeClosure(first, last, allHashes);

    cout << "digraph G {\n";

    for (set<string>::iterator it = allHashes.begin();
         it != allHashes.end(); it++)
    {
        Params pkgImports, fileImports, arguments;
        readPkgDescr(*it, pkgImports, fileImports, arguments);

        cout << dotQuote(*it) << "[label = \"" 
             << getFromEnv(arguments, "id")
             << "\"];\n";

        for (Params::iterator it2 = pkgImports.begin();
             it2 != pkgImports.end(); it2++)
            cout << dotQuote(it2->second) << " -> " 
                 << dotQuote(*it) << ";\n";
    }

    cout << "}\n";
}


void fetch(string id)
{
    string fn;

    /* Fetch the object referenced by id. */
    if (isHash(id)) {
        throw Error("not implemented");
    } else {
        fn = fetchURL(id);
    }

    /* Register it by hash. */
    string hash = registerFile(fn);
    cout << hash << endl;
}


void fetch(Strings::iterator first, Strings::iterator last)
{
    for (Strings::iterator it = first; it != last; it++)
        fetch(*it);
}


void printUsage()
{
    cerr <<
"Usage: nix SUBCOMMAND OPTIONS...

Subcommands:

  init
    Initialize the database.

  verify
    Remove stale entries from the database.

  regfile FILENAME...
    Register each FILENAME keyed by its hash.

  reginst HASH PATH
    Register an installed package.

  getpkg HASH...
    For each HASH, ensure that the package referenced by HASH is
    installed. Print out the path of the installation on stdout.

  delpkg HASH...
    Uninstall the package referenced by each HASH, disregarding any
    dependencies that other packages may have on HASH.

  listinst
    Prints a list of installed packages.

  run HASH ARGS...
    Run the descriptor referenced by HASH with the given arguments.

  ensure HASH...
    Like getpkg, but if HASH refers to a run descriptor, fetch only
    the dependencies.

  export DIR HASH...
    Export installed packages to DIR.

  regprebuilt HASH1 HASH2
    Inform Nix that an export HASH2 can be used to fast-build HASH1.

  info HASH...
    Print information about the specified descriptors.

  closure HASH...
    Determine the closure of the set of descriptors under the import
    relation, starting at the given roots.

  graph HASH...
    Like closure, but print a dot graph specification.

  fetch ID...  
    Fetch the objects identified by ID and place them in the Nix
    sources directory.  ID can be a hash or URL.  Print out the hash
    of the object.
";
}


void run(Strings::iterator argCur, Strings::iterator argEnd)
{
    umask(0022);

    char * homeDir = getenv(nixHomeDirEnvVar.c_str());
    if (homeDir) nixHomeDir = homeDir;

    nixSourcesDir = nixHomeDir + "/var/nix/sources";
    nixDB = nixHomeDir + "/var/nix/pkginfo.db";

    /* Parse the global flags. */
    for ( ; argCur != argEnd; argCur++) {
        string arg(*argCur);
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return;
        } else if (arg[0] == '-') {
            throw UsageError("invalid option `" + arg + "'");
        } else break;
    }

    UsageError argcError("wrong number of arguments");

    /* Parse the command. */
    if (argCur == argEnd) throw UsageError("no command specified");
    string cmd = *argCur++;
    int argc = argEnd - argCur;

    if (cmd == "init") {
        if (argc != 0) throw argcError;
        initDB();
    } else if (cmd == "verify") {
        if (argc != 0) throw argcError;
        verifyDB();
    } else if (cmd == "getpkg") {
        for (Strings::iterator it = argCur; it != argEnd; it++) {
            string path = getPkg(*it);
            cout << path << endl;
        }
    } else if (cmd == "delpkg") {
        for_each(argCur, argEnd, delPkg);
    } else if (cmd == "run") {
        if (argc < 1) throw argcError;
        runPkg(*argCur, argCur + 1, argEnd);
    } else if (cmd == "ensure") {
        for_each(argCur, argEnd, ensurePkg);
    } else if (cmd == "export") {
        if (argc < 1) throw argcError;
        exportPkgs(*argCur, argCur + 1, argEnd);
    } else if (cmd == "regprebuilt") {
        if (argc != 2) throw argcError;
        registerPrebuilt(*argCur, argCur[1]);
    } else if (cmd == "regfile") {
        for_each(argCur, argEnd, registerFile);
    } else if (cmd == "regurl") {
        registerURL(argCur[0], argCur[1]);
    } else if (cmd == "reginst") {
        if (argc != 2) throw argcError;
        registerInstalledPkg(*argCur, argCur[1]);
    } else if (cmd == "listinst") {
        if (argc != 0) throw argcError;
        listInstalledPkgs();
    } else if (cmd == "info") {
        printInfo(argCur, argEnd);
    } else if (cmd == "closure") {
        printClosure(argCur, argEnd);
    } else if (cmd == "graph") {
        printGraph(argCur, argEnd);
    } else if (cmd == "fetch") {
        fetch(argCur, argEnd);
    } else
        throw UsageError("unknown command: " + string(cmd));
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
             << "Try `nix -h' for more information.\n";
        return 1;
    } catch (exception & e) {
        cerr << "error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
