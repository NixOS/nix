#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "exec.hh"
#include "util.hh"
#include "globals.hh"


class AutoDelete
{
    string path;
    bool del;
public:

    AutoDelete(const string & p) : path(p) 
    {
        del = true;
    }

    ~AutoDelete()
    {
        if (del) deletePath(path);
    }

    void cancel()
    {
        del = false;
    }
};


/* Run a program. */
void runProgram(const string & program, Environment env)
{
    /* Create a log file. */
    string logFileName = nixLogDir + "/run.log";
    /* !!! auto-pclose on exit */
    FILE * logFile = popen(("tee -a " + logFileName + " >&2").c_str(), "w"); /* !!! escaping */
    if (!logFile)
        throw SysError(format("creating log file `%1%'") % logFileName);

    /* Create a temporary directory where the build will take
       place. */
    static int counter = 0;
    string tmpDir = (format("/tmp/nix-%1%-%2%") % getpid() % counter++).str();

    if (mkdir(tmpDir.c_str(), 0777) == -1)
        throw SysError(format("creating directory `%1%'") % tmpDir);

    AutoDelete delTmpDir(tmpDir);

    /* Fork a child to build the package. */
    pid_t pid;
    switch (pid = fork()) {
            
    case -1:
        throw SysError("unable to fork");

    case 0: 

        try { /* child */

            if (chdir(tmpDir.c_str()) == -1)
                throw SysError(format("changing into to `%1%'") % tmpDir);

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
    
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        delTmpDir.cancel();
        throw Error("unable to build package");
    }
}


