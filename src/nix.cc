#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <sstream>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <cstdio>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <db4/db_cxx.h>

extern "C" {
#include <aterm1.h>
}

extern "C" {
#include "md5.h"
}

#include "util.hh"

using namespace std;


/* Database names. */
static string dbRefs = "refs";
static string dbInstPkgs = "pkginst";
static string dbPrebuilts = "prebuilts";


/* The canonical system name, as returned by config.guess. */ 
static string thisSystem = SYSTEM;


/* The prefix of the Nix installation, and the environment variable
   that can be used to override the default. */
static string nixHomeDir = "/nix";
static string nixHomeDirEnvVar = "NIX";


/* Wrapper classes that ensures that the database is closed upon
   object destruction. */
class Db2 : public Db 
{
public:
    Db2(DbEnv *env, u_int32_t flags) : Db(env, flags) { }
    ~Db2() { close(0); }
};


class DbcClose 
{
    Dbc * cursor;
public:
    DbcClose(Dbc * c) : cursor(c) { }
    ~DbcClose() { cursor->close(); }
};


auto_ptr<Db2> openDB(const string & dbname, bool readonly)
{
    auto_ptr<Db2> db;

    db = auto_ptr<Db2>(new Db2(0, 0));

    db->open((nixHomeDir + "/var/nix/pkginfo.db").c_str(), dbname.c_str(),
        DB_HASH, readonly ? DB_RDONLY : DB_CREATE, 0666);

    return db;
}


bool queryDB(const string & dbname, const string & key, string & data)
{
    int err;
    auto_ptr<Db2> db = openDB(dbname, true);

    Dbt kt((void *) key.c_str(), key.length());
    Dbt dt;

    err = db->get(0, &kt, &dt, 0);
    if (err) return false;

    data = string((char *) dt.get_data(), dt.get_size());
    
    return true;
}


void setDB(const string & dbname, const string & key, const string & data)
{
    auto_ptr<Db2> db = openDB(dbname, false);
    Dbt kt((void *) key.c_str(), key.length());
    Dbt dt((void *) data.c_str(), data.length());
    db->put(0, &kt, &dt, 0);
}


void delDB(const string & dbname, const string & key)
{
    auto_ptr<Db2> db = openDB(dbname, false);
    Dbt kt((void *) key.c_str(), key.length());
    db->del(0, &kt, 0);
}


typedef pair<string, string> DBPair;
typedef list<DBPair> DBPairs;


void enumDB(const string & dbname, DBPairs & contents)
{
    auto_ptr<Db2> db = openDB(dbname, false);

    Dbc * cursor;
    db->cursor(0, &cursor, 0);
    DbcClose cursorCloser(cursor);

    Dbt kt, dt;
    while (cursor->get(&kt, &dt, DB_NEXT) != DB_NOTFOUND) {
        string key((char *) kt.get_data(), kt.get_size());
        string data((char *) dt.get_data(), dt.get_size());
        contents.push_back(DBPair(key, data));
    }
}


string printHash(unsigned char * buf)
{
    ostringstream str;
    for (int i = 0; i < 16; i++) {
        str.fill('0');
        str.width(2);
        str << hex << (int) buf[i];
    }
    return str.str();
}

    
/* Verify that a reference is valid (that is, is a MD5 hash code). */
void checkHash(const string & s)
{
    string err = "invalid reference: " + s;
    if (s.length() != 32)
        throw BadRefError(err);
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f')))
            throw BadRefError(err);
    }
}


/* Compute the MD5 hash of a file. */
string hashFile(string filename)
{
    unsigned char hash[16];
    FILE * file = fopen(filename.c_str(), "rb");
    if (!file)
        throw BadRefError("file `" + filename + "' does not exist");
    int err = md5_stream(file, hash);
    fclose(file);
    if (err) throw BadRefError("cannot hash file");
    return printHash(hash);
}


typedef map<string, string> Params;


void readPkgDescr(const string & hash,
    Params & pkgImports, Params & fileImports, Params & arguments)
{
    string pkgfile;

    if (!queryDB(dbRefs, hash, pkgfile))
        throw Error("unknown package " + hash);

    //    cerr << "reading information about " + hash + " from " + pkgfile + "\n";

    /* Verify that the file hasn't changed. !!! race */
    if (hashFile(pkgfile) != hash)
        throw Error("file " + pkgfile + " is stale");

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
        else throw Error("invalid binding in " + pkgfile);
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

        if (!queryDB(dbRefs, it->second, file))
            throw Error("unknown file " + it->second);

        if (hashFile(file) != it->second)
            throw Error("file " + file + " is stale");

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
            if (queryDB(dbPrebuilts, hash, prebuiltHash) &&
                queryDB(dbRefs, prebuiltHash, prebuiltFile)) 
            {
                cerr << "substituting prebuilt " << prebuiltFile << endl;

                if (hashFile(prebuiltFile) != prebuiltHash) {
                    cerr << "prebuilt " + prebuiltFile + " is stale\n";
                    goto build;
                }

                int res = system(("tar xvfj " + prebuiltFile).c_str()); // !!! escaping
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
    
    } catch (exception &) {
        system(("rm -rf " + path).c_str());
        throw;
    }

    setDB(dbInstPkgs, hash, path);
}


string getPkg(string hash)
{
    string path;
    checkHash(hash);
    while (!queryDB(dbInstPkgs, hash, path))
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
    if (queryDB(dbInstPkgs, hash, path)) {
        int res = system(("rm -rf " + path).c_str()); // !!! escaping
        delDB(dbInstPkgs, hash); // not a bug
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot delete " + path);
    }
}


void exportPkgs(string outDir, 
    Strings::iterator firstHash, 
    Strings::iterator lastHash)
{
    for (Strings::iterator it = firstHash; it != lastHash; it++) {
        string hash = *it;
        string pkgDir = getPkg(hash);
        string tmpFile = outDir + "/export_tmp";

        int res = system(("cd " + pkgDir + " && tar cvfj " + tmpFile + " .").c_str()); // !!! escaping
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot tar " + pkgDir);

        string prebuiltHash = hashFile(tmpFile);
        string pkgId = queryPkgId(hash);
        string prebuiltFile = outDir + "/" +
            pkgId + "-" + hash + "-" + prebuiltHash + ".tar.bz2";
        
        rename(tmpFile.c_str(), prebuiltFile.c_str());
    }
}


void regPrebuilt(string pkgHash, string prebuiltHash)
{
    checkHash(pkgHash);
    checkHash(prebuiltHash);
    setDB(dbPrebuilts, pkgHash, prebuiltHash);
}


string absPath(string filename)
{
    if (filename[0] != '/') {
        char buf[PATH_MAX];
        if (!getcwd(buf, sizeof(buf)))
            throw Error("cannot get cwd");
        filename = string(buf) + "/" + filename;
        /* !!! canonicalise */
    }
    return filename;
}


void registerFile(string filename)
{
    filename = absPath(filename);
    setDB(dbRefs, hashFile(filename), filename);
}


/* This is primarily used for bootstrapping. */
void registerInstalledPkg(string hash, string path)
{
    checkHash(hash);
    if (path == "")
        delDB(dbInstPkgs, hash);
    else
        setDB(dbInstPkgs, hash, path);
}


void initDB()
{
    openDB(dbRefs, false);
    openDB(dbInstPkgs, false);
    openDB(dbPrebuilts, false);
}


void verifyDB()
{
    /* Check that all file references are still valid. */
    DBPairs fileRefs;
    
    enumDB(dbRefs, fileRefs);

    for (DBPairs::iterator it = fileRefs.begin();
         it != fileRefs.end(); it++)
    {
        try {
            if (hashFile(it->second) != it->first) {
                cerr << "file " << it->second << " has changed\n";
                delDB(dbRefs, it->first);
            }
        } catch (BadRefError e) { /* !!! better error check */ 
            cerr << "file " << it->second << " has disappeared\n";
            delDB(dbRefs, it->first);
        }
    }

    /* Check that all installed packages are still there. */
    DBPairs instPkgs;

    enumDB(dbInstPkgs, instPkgs);

    for (DBPairs::iterator it = instPkgs.begin();
         it != instPkgs.end(); it++)
    {
        struct stat st;
        if (stat(it->second.c_str(), &st) == -1) {
            cerr << "package " << it->first << " has disappeared\n";
            delDB(dbInstPkgs, it->first);
        }
    }

    /* TODO: check that all directories in pkgHome are installed
       packages. */
}


void listInstalledPkgs()
{
    DBPairs instPkgs;

    enumDB(dbInstPkgs, instPkgs);

    for (DBPairs::iterator it = instPkgs.begin();
         it != instPkgs.end(); it++)
        cout << it->first << endl;
}


void printInfo(Strings::iterator first, Strings::iterator last)
{
    for (Strings::iterator it = first; it != last; it++)
    {
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


void run(Strings args)
{
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

  regfile FILENAME
    Register FILENAME keyed by its hash.

  reginst HASH PATH
    Register an installed package.

  getpkg HASH
    Ensure that the package referenced by HASH is installed. Print
    out the path of the package on stdout.

  delpkg HASH
    Uninstall the package referenced by HASH, disregarding any
    dependencies that other packages may have on HASH.

  listinst
    Prints a list of installed packages.

  run HASH ARGS...
    Run the descriptor referenced by HASH with the given arguments.

  ensure HASH
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
";
}


void run(Strings::iterator argCur, Strings::iterator argEnd)
{
    umask(0022);

    char * homeDir = getenv(nixHomeDirEnvVar.c_str());
    if (homeDir) nixHomeDir = homeDir;

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
        if (argc != 1) throw argcError;
        string path = getPkg(*argCur);
        cout << path << endl;
    } else if (cmd == "delpkg") {
        if (argc != 1) throw argcError;
        delPkg(*argCur);
    } else if (cmd == "run") {
        if (argc < 1) throw argcError;
        runPkg(*argCur, argCur + 1, argEnd);
    } else if (cmd == "ensure") {
        if (argc != 1) throw argcError;
        ensurePkg(*argCur);
    } else if (cmd == "export") {
        if (argc < 1) throw argcError;
        exportPkgs(*argCur, argCur + 1, argEnd);
    } else if (cmd == "regprebuilt") {
        if (argc != 2) throw argcError;
        regPrebuilt(*argCur, argCur[1]);
    } else if (cmd == "regfile") {
        if (argc != 1) throw argcError;
        registerFile(*argCur);
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
        try {
            run(argCur, argEnd);
        } catch (DbException e) {
            throw Error(e.what());
        }
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
