#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <db4/db.h>


#define PKGINFO_PATH "/pkg/sys/var/pkginfo"


typedef enum {true = 1, false = 0} bool;


static char * prog;
static char * dbfile = PKGINFO_PATH;


DB * openDB(char * dbname, bool readonly)
{
    int err;
    DB * db;

    err = db_create(&db, 0, 0);
    if (err) {
        fprintf(stderr, "error creating db handle: %s\n",
            db_strerror(err));
        return 0;
    }
        
    err = db->open(db, dbfile, dbname,
        DB_HASH, readonly ? DB_RDONLY : DB_CREATE, 0666);
    if (err) {
        fprintf(stderr, "creating opening %s: %s\n",
            dbfile, db_strerror(err));
        db->close(db, 0);
        return 0;
    }

    return db;
}


bool queryDB(char * dbname, char * key, char * * data)
{
    DB * db = 0;
    DBT kt, dt;
    int err;

    db = openDB(dbname, true);
    if (!db) goto bad;

    *data = 0;

    memset(&kt, 0, sizeof(kt));
    memset(&dt, 0, sizeof(dt));
    kt.size = strlen(key);
    kt.data = key;

    err = db->get(db, 0, &kt, &dt, 0);
    if (!err) {
        *data = malloc(dt.size + 1);
        memcpy(*data, dt.data, dt.size);
        (*data)[dt.size] = 0;
    } else if (err != DB_NOTFOUND) {
        fprintf(stderr, "creating opening %s: %s\n",
            dbfile, db_strerror(err));
        goto bad;
    }

    db->close(db, 0);
    return true;

 bad:
    if (db) db->close(db, 0);
    return false;
}


bool setDB(char * dbname, char * key, char * data)
{
    DB * db = 0;
    DBT kt, dt;
    int err;

    db = openDB(dbname, false);
    if (!db) goto bad;

    memset(&kt, 0, sizeof(kt));
    memset(&dt, 0, sizeof(dt));
    kt.size = strlen(key);
    kt.data = key;
    dt.size = strlen(data);
    dt.data = data;

    err = db->put(db, 0, &kt, &dt, 0);
    if (err) {
        fprintf(stderr, "error storing data in %s: %s\n",
            dbfile, db_strerror(err));
        goto bad;
    }

    db->close(db, 0);
    return true;

 bad:
    if (db) db->close(db, 0);
    return false;
}


bool delDB(char * dbname, char * key)
{
    DB * db = 0;
    DBT kt;
    int err;

    db = openDB(dbname, false);
    if (!db) goto bad;

    memset(&kt, 0, sizeof(kt));
    kt.size = strlen(key);
    kt.data = key;

    err = db->del(db, 0, &kt, 0);
    if (err) {
        fprintf(stderr, "error deleting data from %s: %s\n",
            dbfile, db_strerror(err));
        goto bad;
    }

    db->close(db, 0);
    return true;

 bad:
    if (db) db->close(db, 0);
    return false;
}


bool getPkg(int argc, char * * argv)
{
    char * pkg;
    char * src = 0;
    char * inst = 0;
    char inst2[1024];
    char cmd[2048];
    int res;

    if (argc != 1) {
        fprintf(stderr, "arguments missing in get-pkg\n");
        return false;
    }

    pkg = argv[0];

    if (!queryDB("pkginst", pkg, &inst)) return false;
    
    if (inst) {
        printf("%s\n", inst);
        free(inst);
    } else {

        fprintf(stderr, "package %s is not yet installed\n", pkg);
        
        if (!queryDB("pkgsrc", pkg, &src)) return false;

        if (!src) {
            fprintf(stderr, "source of package %s is not known\n", pkg);
            return false;
        }

        if (snprintf(inst2, sizeof(inst2), "/pkg/%s", pkg) >= sizeof(inst2)) {
            fprintf(stderr, "buffer overflow\n");
            free(src);
            return false;
        }

        if (snprintf(cmd, sizeof(cmd), "rsync -a \"%s\"/ \"%s\"",
                src, inst2) >= sizeof(cmd)) 
        {
            fprintf(stderr, "buffer overflow\n");
            free(src);
            return false;
        }
        
        res = system(cmd);
        if (!WIFEXITED(res) || WEXITSTATUS(res) != 0) {
            fprintf(stderr, "unable to copy sources\n");
            free(src);
            return false;
        }

        if (chdir(inst2)) {
            fprintf(stderr, "unable to chdir to package directory\n");
            free(src);
            return false;
        }

        /* Prepare for building.  Clean the environment so that the
           build process does not inherit things it shouldn't. */
        setenv("PATH", "/pkg/sys/bin", 1);

        res = system("./buildme");
        if (!WIFEXITED(res) || WEXITSTATUS(res) != 0) {
            fprintf(stderr, "unable to build package\n");
            free(src);
            return false;
        }

        setDB("pkginst", pkg, inst2);

        free(src);

        printf("%s\n", inst2);
    }

    return true;
}


bool registerPkg(int argc, char * * argv)
{
    char * pkg;
    char * src;
    
    if (argc != 2) {
        fprintf(stderr, "arguments missing in register-pkg\n");
        return false;
    }

    pkg = argv[0];
    src = argv[1];

    return setDB("pkgsrc", pkg, src);
}


/* This is primarily used for bootstrapping. */
bool registerInstalledPkg(int argc, char * * argv)
{
    char * pkg;
    char * inst;
    
    if (argc != 2) {
        fprintf(stderr, "arguments missing in register-installed-pkg\n");
        return false;
    }

    pkg = argv[0];
    inst = argv[1];
    
    if (strcmp(inst, "") == 0)
        return delDB("pkginst", pkg);
    else
        return setDB("pkginst", pkg, inst);
}


bool run(int argc, char * * argv)
{
    char * cmd;

    if (argc < 1) {
        fprintf(stderr, "command not specified\n");
        return false;
    }

    cmd = argv[0];
    argc--, argv++;

    if (strcmp(cmd, "get-pkg") == 0)
        return getPkg(argc, argv);
    else if (strcmp(cmd, "register-pkg") == 0)
        return registerPkg(argc, argv);
    else if (strcmp(cmd, "register-installed-pkg") == 0)
        return registerInstalledPkg(argc, argv);
    else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        return false;
    }
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
            fprintf(stderr, "unknown option\n");
            break;
        }

    }

    argc -= optind, argv += optind;

    if (!run(argc, argv)) 
        return 1;
    else
        return 0;
}
