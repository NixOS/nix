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


static string pathNullDevice = "/dev/null";


/* Run a program. */
void runProgram(const string & program, 
    const Strings & args, const Environment & env)
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

            /* Fill in the arguments. */
            const char * argArr[args.size() + 2];
            const char * * p = argArr;
            string progName = baseNameOf(program);
            *p++ = progName.c_str();
            for (Strings::const_iterator i = args.begin();
                 i != args.end(); i++)
                *p++ = i->c_str();
            *p = 0;

            /* Fill in the environment. */
            Strings envStrs;
            const char * envArr[env.size() + 1];
            p = envArr;
            for (Environment::const_iterator i = env.begin();
                 i != env.end(); i++)
                *p++ = envStrs.insert(envStrs.end(), 
                    i->first + "=" + i->second)->c_str();
            *p = 0;

            /* Dup the log handle into stderr. */
            if (dup2(fileno(logFile), STDERR_FILENO) == -1)
                throw SysError("cannot pipe standard error into log file");
            
            /* Dup stderr to stdin. */
            if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
                throw SysError("cannot dup stderr into stdout");

	    /* Reroute stdin to /dev/null. */
	    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
	    if (fdDevNull == -1)
		throw SysError(format("cannot open `%1%'") % pathNullDevice);
	    if (dup2(fdDevNull, STDIN_FILENO) == -1)
		throw SysError("cannot dup null device into stdin");

            /* Execute the program.  This should not return. */
            execve(program.c_str(), (char * *) argArr, (char * *) envArr);

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
	if (keepFailed) {
	    msg(lvlTalkative, 
		format("build failed; keeping build directory `%1%'") % tmpDir);
	    delTmpDir.cancel();
	}
        throw Error("unable to build package");
    }
}


