#include <map>
#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "eval.hh"
#include "globals.hh"
#include "values.hh"
#include "db.hh"


/* A Unix environment is a mapping from strings to strings. */
typedef map<string, string> Environment;


/* Return true iff the given path exists. */
bool pathExists(string path)
{
    int res;
    struct stat st;
    res = stat(path.c_str(), &st);
    if (!res) return true;
    if (errno != ENOENT)
        throw SysError("getting status of " + path);
    return false;
}


#if 0
/* Compute a derived value by running a program. */
static Hash computeDerived(Hash sourceHash, string targetName,
    string platform, Hash prog, Environment env)
{
    string targetPath = nixValues + "/" + 
        (string) sourceHash + "-nf";

    /* Check whether the target already exists. */
    if (pathExists(targetPath)) 
        throw Error("derived value in " + targetPath + " already exists");

    /* Find the program corresponding to the hash `prog'. */
    string progPath = queryValuePath(prog);

    /* Finalize the environment. */
    env["out"] = targetPath;

    /* Create a log file. */
    string logFileName = 
        nixLogDir + "/" + baseNameOf(targetPath) + ".log";
    /* !!! auto-pclose on exit */
    FILE * logFile = popen(("tee " + logFileName + " >&2").c_str(), "w"); /* !!! escaping */
    if (!logFile)
        throw SysError("unable to create log file " + logFileName);

    try {

        /* Fork a child to build the package. */
        pid_t pid;
        switch (pid = fork()) {
            
        case -1:
            throw SysError("unable to fork");

        case 0: 

            try { /* child */

#if 0
                /* Try to use a prebuilt. */
                string prebuiltHashS, prebuiltFile;
                if (queryDB(nixDB, dbPrebuilts, hash, prebuiltHashS)) {

                    try {
                        prebuiltFile = getFile(parseHash(prebuiltHashS));
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
#endif

//             build:

                /* Fill in the environment.  We don't bother freeing
                   the strings, since we'll exec or die soon
                   anyway. */
                const char * env2[env.size() + 1];
                int i = 0;
                for (Environment::iterator it = env.begin();
                     it != env.end(); it++, i++)
                    env2[i] = (new string(it->first + "=" + it->second))->c_str();
                env2[i] = 0;

                /* Dup the log handle into stderr. */
                if (dup2(fileno(logFile), STDERR_FILENO) == -1)
                    throw Error("cannot pipe standard error into log file: " + string(strerror(errno)));
            
                /* Dup stderr to stdin. */
                if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
                    throw Error("cannot dup stderr into stdout");

                /* Make the program executable.  !!! hack. */
                if (chmod(progPath.c_str(), 0755))
                    throw Error("cannot make program executable");

                /* Execute the program.  This should not return. */
                execle(progPath.c_str(), baseNameOf(progPath).c_str(), 0, env2);

                throw Error("unable to execute builder: " +
                    string(strerror(errno)));
            
            } catch (exception & e) {
                cerr << "build error: " << e.what() << endl;
            }
            _exit(1);

        }

        /* parent */

        /* Close the logging pipe.  Note that this should not cause
           the logger to exit until builder exits (because the latter
           has an open file handle to the former). */
        pclose(logFile);
    
        /* Wait for the child to finish. */
        int status;
        if (waitpid(pid, &status, 0) != pid)
            throw Error("unable to wait for child");
    
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw Error("unable to build package");

        /* Check whether the result was created. */
        if (!pathExists(targetPath))
            throw Error("program " + progPath + 
                " failed to create a result in " + targetPath);

#if 0
        /* Remove write permission from the value. */
        int res = system(("chmod -R -w " + targetPath).c_str()); // !!! escaping
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot remove write permission from " + targetPath);
#endif

    } catch (exception &) {
//         system(("rm -rf " + targetPath).c_str());
        throw;
    }

    /* Hash the result. */
    Hash targetHash = hashPath(targetPath);

    /* Register targetHash -> targetPath.  !!! this should be in
       values.cc. */
    setDB(nixDB, dbRefs, targetHash, targetName);

    /* Register that targetHash was produced by evaluating
       sourceHash; i.e., that targetHash is a normal form of
       sourceHash. !!! this shouldn't be here */
    setDB(nixDB, dbNFs, sourceHash, targetHash);

    return targetHash;
}
#endif


/* Throw an exception if the given platform string is not supported by
   the platform we are executing on. */
static void checkPlatform(string platform)
{
    if (platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
            % platform % thisSystem);
}


string printTerm(ATerm t)
{
    char * s = ATwriteToString(t);
    return s;
}


/* Throw an exception with an error message containing the given
   aterm. */
static Error badTerm(const format & f, ATerm t)
{
    return Error(format("%1%, in `%2%'") % f.str() % printTerm(t));
}


Hash hashTerm(ATerm t)
{
    return hashString(printTerm(t));
}


#if 0
/* Evaluate a list of arguments into normal form. */
void evalArgs(ATermList args, ATermList & argsNF, Environment & env)
{
    argsNF = ATempty;

    while (!ATisEmpty(args)) {
        ATerm eName, eVal, arg = ATgetFirst(args);
        if (!ATmatch(arg, "Tup(<term>, <term>)", &eName, &eVal))
            throw badTerm("invalid argument", arg);

        string name = evalString(eName);
        eVal = evalValue(eVal);

        char * s;
        if (ATmatch(eVal, "Str(<str>)", &s)) {
            env[name] = s;
        } else if (ATmatch(eVal, "Hash(<str>)", &s)) {
            env[name] = queryValuePath(parseHash(s));
        } else throw badTerm("invalid argument value", eVal);

        argsNF = ATinsert(argsNF,
            ATmake("Tup(Str(<str>), <term>)", name.c_str(), eVal));

        args = ATgetNext(args);
    }

    argsNF = ATreverse(argsNF);
}
#endif


struct RStatus
{
    /* !!! the comparator of this hash should match the semantics of
       the file system */
//     map<string, Hash> paths;
};


static void realise(RStatus & status, FState fs)
{
    char * s;
    Content content;
    ATermList refs;
    
    if (ATmatch(fs, "File(<str>, <term>, [<list>])", &s, &content, &refs)) {
        string path(s);

        if (path[0] != '/') throw Error("absolute path expected: " + path);

        /* Realise referenced paths. */
        while (!ATisEmpty(refs)) {
            realise(status, ATgetFirst(refs));
            refs = ATgetNext(refs);
        }

        if (!ATmatch(content, "Hash(<str>)", &s))
            throw badTerm("hash expected", content);
        Hash hash = parseHash(s);

        /* Perhaps the path already exists and has the right hash? */
        if (pathExists(path)) {
            if (hash == hashPath(path)) {
                debug(format("path %1% already has hash %2%")
                    % path % (string) hash);
                return;
            }

            throw Error(format("path %1% exists, but does not have hash %2%")
                % path % (string) hash);
        }

        /* Do we know a path with that hash?  If so, copy it. */
        string path2 = queryFromStore(hash);
        copyFile(path2, path);
    }

    else if (ATmatch(fs, "Derive()")) {
        
        
    }

    else throw badTerm("bad file system state expression", fs);
}


void realiseFState(FState fs)
{
    RStatus status;
    realise(status, fs);
}
