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
bool pathExists(const string & path)
{
    int res;
    struct stat st;
    res = stat(path.c_str(), &st);
    if (!res) return true;
    if (errno != ENOENT)
        throw SysError(format("getting status of %1%") % path);
    return false;
}


/* Run a program. */
static void runProgram(const string & program, Environment env)
{
    /* Create a log file. */
    string logFileName = nixLogDir + "/run.log";
    /* !!! auto-pclose on exit */
    FILE * logFile = popen(("tee " + logFileName + " >&2").c_str(), "w"); /* !!! escaping */
    if (!logFile)
        throw SysError(format("unable to create log file %1%") % logFileName);

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
                throw SysError("cannot pipe standard error into log file");
            
            /* Dup stderr to stdin. */
            if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
                throw SysError("cannot dup stderr into stdout");

            /* Make the program executable.  !!! hack. */
            if (chmod(program.c_str(), 0755))
                throw SysError("cannot make program executable");

            /* Execute the program.  This should not return. */
            execle(program.c_str(), baseNameOf(program).c_str(), 0, env2);

            throw SysError(format("unable to execute %1%") % program);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
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
}


/* Throw an exception if the given platform string is not supported by
   the platform we are executing on. */
static void checkPlatform(const string & platform)
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


struct RStatus
{
    /* !!! the comparator of this hash should match the semantics of
       the file system */
//     map<string, Hash> paths;
};


static ATerm termFromHash(const Hash & hash)
{
    string path = queryFromStore(hash);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm %1%") % path);
    return t;
}


static Hash writeTerm(ATerm t)
{
    string path = nixStore + "/tmp.nix"; /* !!! */
    if (!ATwriteToNamedTextFile(t, path.c_str()))
        throw Error(format("cannot write aterm %1%") % path);
    Hash hash = hashPath(path);
    string path2 = nixStore + "/" + (string) hash + ".nix";
    if (rename(path.c_str(), path2.c_str()) == -1)
        throw SysError(format("renaming %1% to %2%") % path % path2);
    setDB(nixDB, dbRefs, hash, path2);
    return hash;
}


static FState realise(RStatus & status, FState fs)
{
    char * s1, * s2, * s3;
    Content content;
    ATermList refs, ins, bnds;

    /* First repeatedly try to substitute $fs$ by any known successors
       in order to speed up the rewrite process. */
    {
        string fsHash, scHash;
        while (queryDB(nixDB, dbSuccessors, fsHash = hashTerm(fs), scHash)) {
            debug(format("successor %1% -> %2%") % (string) fsHash % scHash);
            FState fs2 = termFromHash(parseHash(scHash));
            if (fs == fs2) {
                debug(format("successor cycle detected in %1%") % printTerm(fs));
                break;
            }
            fs = fs2;
        }
    }

    /* Fall through. */

    if (ATmatch(fs, "Include(<str>)", &s1)) {
        return realise(status, termFromHash(parseHash(s1)));
    }
    
    else if (ATmatch(fs, "File(<str>, <term>, [<list>])", &s1, &content, &refs)) {
        string path(s1);

        msg(format("realising atomic path %1%") % path);
        Nest nest(true);

        if (path[0] != '/') throw Error("absolute path expected: " + path);

        /* Realise referenced paths. */
        ATermList refs2 = ATempty;
        while (!ATisEmpty(refs)) {
            refs2 = ATappend(refs2, realise(status, ATgetFirst(refs)));
            refs = ATgetNext(refs);
        }
        refs2 = ATreverse(refs2);

        if (!ATmatch(content, "Hash(<str>)", &s1))
            throw badTerm("hash expected", content);
        Hash hash = parseHash(s1);

        /* Normal form. */
        ATerm nf = ATmake("File(<str>, <term>, <term>)",
            path.c_str(), content, refs2);

        /* Register the normal form. */
        if (fs != nf) {
            Hash nfHash = writeTerm(nf);
            setDB(nixDB, dbSuccessors, hashTerm(fs), nfHash);
        }

        /* Perhaps the path already exists and has the right hash? */
        if (pathExists(path)) {
            if (hash == hashPath(path)) {
                debug(format("path %1% already has hash %2%")
                    % path % (string) hash);
                return nf;
            }

            throw Error(format("path %1% exists, but does not have hash %2%")
                % path % (string) hash);
        }

        /* Do we know a path with that hash?  If so, copy it. */
        string path2 = queryFromStore(hash);
        copyFile(path2, path);

        return nf;
    }

    else if (ATmatch(fs, "Derive(<str>, <str>, [<list>], <str>, [<list>])",
                 &s1, &s2, &ins, &s3, &bnds)) 
    {
        string platform(s1), builder(s2), outPath(s3);

        msg(format("realising derivate path %1%") % outPath);
        Nest nest(true);

        checkPlatform(platform);
        
        /* Realise inputs. */
        ATermList ins2 = ATempty;
        while (!ATisEmpty(ins)) {
            ins2 = ATappend(ins2, realise(status, ATgetFirst(ins)));
            ins = ATgetNext(ins);
        }
        ins2 = ATreverse(ins2);

        /* Build the environment. */
        Environment env;
        while (!ATisEmpty(bnds)) {
            ATerm bnd = ATgetFirst(bnds);
            if (!ATmatch(bnd, "(<str>, <str>)", &s1, &s2))
                throw badTerm("string expected", bnd);
            env[s1] = s2;
            bnds = ATgetNext(bnds);
        }

        /* Check whether the target already exists. */
        if (pathExists(outPath))
            deleteFromStore(outPath);
//             throw Error(format("path %1% already exists") % outPath);

        /* Run the builder. */
        runProgram(builder, env);
        
        /* Check whether the result was created. */
        if (!pathExists(outPath))
            throw Error(format("program %1% failed to create a result in %2%")
                % builder % outPath);

#if 0
        /* Remove write permission from the value. */
        int res = system(("chmod -R -w " + targetPath).c_str()); // !!! escaping
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot remove write permission from " + targetPath);
#endif

        /* Hash the result. */
        Hash outHash = hashPath(outPath);

        /* Register targetHash -> targetPath.  !!! this should be in
           values.cc. */
        setDB(nixDB, dbRefs, outHash, outPath);

        /* Register the normal form of fs. */
        FState nf = ATmake("File(<str>, Hash(<str>), <term>)",
            outPath.c_str(), ((string) outHash).c_str(), ins2);
        Hash nfHash = writeTerm(nf);
        setDB(nixDB, dbSuccessors, hashTerm(fs), nfHash);

        return nf;
    }

    throw badTerm("bad fstate expression", fs);
}


FState realiseFState(FState fs)
{
    RStatus status;
    return realise(status, fs);
}
