#include <iostream>
#include <memory>
#include <string>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <db4/db_cxx.h>

using namespace std;


#define PKGINFO_PATH "/pkg/sys/var/pkginfo"


static string prog;
static string dbfile = PKGINFO_PATH;


class Db2 : public Db 
{
public:
    Db2(DbEnv *env, u_int32_t flags)
        : Db(env, flags)
    {
    }

    ~Db2()
    {
        close(0);
    }
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


void getPkg(int argc, char * * argv)
{
    string pkg;
    string src;
    string inst;
    string cmd;
    int res;

    if (argc != 1)
        throw string("arguments missing in get-pkg");

    pkg = argv[0];

    if (queryDB("pkginst", pkg, inst)) {
        cout << inst << endl;
        return;
    }
    
    cerr << "package " << pkg << " is not yet installed\n";
        
    if (!queryDB("pkgsrc", pkg, src)) 
        throw string("source of package " + string(pkg) + " is not known");

    inst = "/pkg/" + pkg;

    cmd = "rsync -a \"" + src + "\"/ \"" + inst + "\"";
        
    res = system(cmd.c_str());
    if (!WIFEXITED(res) || WEXITSTATUS(res) != 0)
        throw string("unable to copy sources");

    if (chdir(inst.c_str()))
        throw string("unable to chdir to package directory");

    /* Prepare for building.  Clean the environment so that the
       build process does not inherit things it shouldn't. */
    setenv("PATH", "/pkg/sys/bin", 1);

    res = system("./buildme");
    if (!WIFEXITED(res) || WEXITSTATUS(res) != 0)
        throw string("unable to build package");

    setDB("pkginst", pkg, inst);

    cout << inst << endl;
}


void registerPkg(int argc, char * * argv)
{
    char * pkg;
    char * src;
    
    if (argc != 2)
        throw string("arguments missing in register-pkg");

    pkg = argv[0];
    src = argv[1];

    setDB("pkgsrc", pkg, src);
}


/* This is primarily used for bootstrapping. */
void registerInstalledPkg(int argc, char * * argv)
{
    string pkg;
    string inst;
    
    if (argc != 2)
        throw string("arguments missing in register-installed-pkg");

    pkg = argv[0];
    inst = argv[1];
    
    if (inst == "")
        delDB("pkginst", pkg);
    else
        setDB("pkginst", pkg, inst);
}


void run(int argc, char * * argv)
{
    string cmd;

    if (argc < 1)
        throw string("command not specified\n");

    cmd = argv[0];
    argc--, argv++;

    if (cmd == "get-pkg")
        getPkg(argc, argv);
    else if (cmd == "register-pkg")
        registerPkg(argc, argv);
    else if (cmd == "register-installed-pkg")
        registerInstalledPkg(argc, argv);
    else
        throw string("unknown command: " + string(cmd));
}
    
    
int main(int argc, char * * argv)
{
    int c;

    prog = argv[0];

    while ((c = getopt(argc, argv, "d:")) != EOF) {
        
        switch (c) {

        case 'd':
            dbfile = optarg;
            break;

        default:
            cerr << "unknown option\n";
            break;
        }

    }

    argc -= optind, argv += optind;

    try {
        run(argc, argv);
        return 0;
    } catch (DbException e) {
        cerr << "db exception: " << e.what() << endl;
        return 1;
    } catch (string s) {
        cerr << s << endl;
        return 1;
    }
}
