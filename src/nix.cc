#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <sstream>
#include <list>
#include <cstdio>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <db4/db_cxx.h>

using namespace std;


#define PKGINFO_ENVVAR "NIX_DB"
#define PKGINFO_PATH "/pkg/sys/var/pkginfo"

#define PKGHOME_ENVVAR "NIX_PKGHOME"


static string dbRefs = "refs";
static string dbInstPkgs = "pkginst";


static string prog;
static string dbfile = PKGINFO_PATH;


static string pkgHome = "/pkg";


class Error : public exception
{
    string err;
public:
    Error(string _err) { err = _err; }
    ~Error() throw () { };
    const char * what() const throw () { return err.c_str(); }
};

class UsageError : public Error
{
public:
    UsageError(string _err) : Error(_err) { };
};

class BadRefError : public Error
{
public:
    BadRefError(string _err) : Error(_err) { };
};


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

    db->open(dbfile.c_str(), dbname.c_str(),
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


/* Verify that a reference is valid (that is, is a MD5 hash code). */
void checkRef(const string & s)
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
string makeRef(string filename)
{
    char hash[33];

    FILE * pipe = popen(("md5sum " + filename + " 2> /dev/null").c_str(), "r");
    if (!pipe) throw BadRefError("cannot execute md5sum");

    if (fread(hash, 32, 1, pipe) != 1)
        throw BadRefError("cannot read hash from md5sum");
    hash[32] = 0;

    pclose(pipe);

    checkRef(hash);
    return hash;
}


struct Dep
{
    string name;
    string ref;
    Dep(string _name, string _ref)
    {
        name = _name;
        ref = _ref;
    }
};

typedef list<Dep> DepList;


void readPkgDescr(const string & pkgfile,
    DepList & pkgImports, DepList & fileImports)
{
    ifstream file;
    file.exceptions(ios::badbit);
    file.open(pkgfile.c_str());
    
    while (!file.eof()) {
        string line;
        getline(file, line);

        int n = line.find('#');
        if (n >= 0) line = line.erase(n);

        if ((int) line.find_first_not_of(" ") < 0) continue;
        
        istringstream str(line);

        string name, op, ref;
        str >> name >> op >> ref;

        checkRef(ref);

        if (op == "<-") 
            pkgImports.push_back(Dep(name, ref));
        else if (op == "=")
            fileImports.push_back(Dep(name, ref));
        else throw Error("invalid operator " + op);
    }
}


string getPkg(string pkgref);


typedef pair<string, string> EnvPair;
typedef list<EnvPair> Environment;


void installPkg(string pkgref)
{
    string pkgfile;
    string src;
    string path;
    string cmd;
    string builder;

    if (!queryDB("refs", pkgref, pkgfile))
        throw Error("unknown package " + pkgref);

    cerr << "installing package " + pkgref + " from " + pkgfile + "\n";

    /* Verify that the file hasn't changed. !!! race */
    if (makeRef(pkgfile) != pkgref)
        throw Error("file " + pkgfile + " is stale");

    /* Read the package description file. */
    DepList pkgImports, fileImports;
    readPkgDescr(pkgfile, pkgImports, fileImports);

    /* Recursively fetch all the dependencies, filling in the
       environment as we go along. */
    Environment env;

    for (DepList::iterator it = pkgImports.begin();
         it != pkgImports.end(); it++)
    {
        cerr << "fetching package dependency "
             << it->name << " <- " << it->ref
             << endl;
        env.push_back(EnvPair(it->name, getPkg(it->ref)));
    }

    for (DepList::iterator it = fileImports.begin();
         it != fileImports.end(); it++)
    {
        cerr << "fetching file dependency "
             << it->name << " = " << it->ref
             << endl;

        string file;

        if (!queryDB("refs", it->ref, file))
            throw Error("unknown file " + it->ref);

        if (makeRef(file) != it->ref)
            throw Error("file " + file + " is stale");

        if (it->name == "build")
            builder = file;
        else
            env.push_back(EnvPair(it->name, file));
    }

    if (builder == "")
        throw Error("no builder specified");

    /* Construct a path for the installed package. */
    path = pkgHome + "/" + pkgref;

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
                cout << "unable to chdir to package directory\n";
                _exit(1);
            }

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

            cout << strerror(errno) << endl;

            cout << "unable to execute builder\n";
            _exit(1);
        }

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

    setDB(dbInstPkgs, pkgref, path);
}


string getPkg(string pkgref)
{
    string path;
    checkRef(pkgref);
    while (!queryDB(dbInstPkgs, pkgref, path))
        installPkg(pkgref);
    return path;
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
    setDB(dbRefs, makeRef(filename), filename);
}


/* This is primarily used for bootstrapping. */
void registerInstalledPkg(string pkgref, string path)
{
    checkRef(pkgref);
    if (path == "")
        delDB(dbInstPkgs, pkgref);
    else
        setDB(dbInstPkgs, pkgref, path);
}


void initDB()
{
    openDB(dbRefs, false);
    openDB(dbInstPkgs, false);
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
            if (makeRef(it->second) != it->first)
                delDB(dbRefs, it->first);
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


void run(int argc, char * * argv)
{
    UsageError argcError("wrong number of arguments");
    string cmd;

    if (argc < 1)
        throw UsageError("no command specified");

    cmd = argv[0];
    argc--, argv++;

    if (cmd == "init") {
        if (argc != 0) throw argcError;
        initDB();
    } else if (cmd == "verify") {
        if (argc != 0) throw argcError;
        verifyDB();
    } else if (cmd == "getpkg") {
        if (argc != 1) throw argcError;
        string path = getPkg(argv[0]);
        cout << path << endl;
    } else if (cmd == "regfile") {
        if (argc != 1) throw argcError;
        registerFile(argv[0]);
    } else if (cmd == "reginst") {
        if (argc != 2) throw argcError;
        registerInstalledPkg(argv[0], argv[1]);
    } else
        throw UsageError("unknown command: " + string(cmd));
}


void printUsage()
{
    cerr <<
"Usage: nix SUBCOMMAND OPTIONS...

Subcommands:

  init
    Initialize the database.

  verify
    Removes stale entries from the database.

  regfile FILENAME
    Register FILENAME keyed by its hash.

  reginst HASH PATH
    Register an installed package.

  getpkg HASH
    Ensure that the package referenced by HASH is installed. Prints
    out the path of the package on stdout.
";
}


void main2(int argc, char * * argv)
{
    int c;

    umask(0022);

    if (getenv(PKGINFO_ENVVAR))
        dbfile = getenv(PKGINFO_ENVVAR);

    if (getenv(PKGHOME_ENVVAR))
        pkgHome = getenv(PKGHOME_ENVVAR);

    opterr = 0;

    while ((c = getopt(argc, argv, "hd:")) != EOF) {
        
        switch (c) {

        case 'h':
            printUsage();
            return;

        case 'd':
            dbfile = optarg;
            break;

        default:
            throw UsageError("invalid option `" + string(1, optopt) + "'");
            break;
        }
    }

    argc -= optind, argv += optind;
    run(argc, argv);
}

    
int main(int argc, char * * argv)
{
    prog = argv[0];

    try { 
        try {

            main2(argc, argv);
            
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
