#include <map>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "normalise.hh"
#include "references.hh"
#include "pathlocks.hh"
#include "globals.hh"


static string pathNullDevice = "/dev/null";


struct Pipe
{
    int readSide, writeSide;

    Pipe();
    ~Pipe();
    void create();
    void closeReadSide();
    void closeWriteSide();
};


Pipe::Pipe()
    : readSide(0), writeSide(0)
{
}


Pipe::~Pipe()
{
    closeReadSide();
    closeWriteSide();
    
}


void Pipe::create()
{
    int fds[2];
    if (pipe(fds) != 0) throw SysError("creating pipe");
    readSide = fds[0];
    writeSide = fds[1];
}


void Pipe::closeReadSide()
{
    if (readSide != 0) {
        if (close(readSide) == -1)
            printMsg(lvlError, format("cannot close read side of pipe"));
        readSide = 0;
    }
}


void Pipe::closeWriteSide()
{
    if (writeSide != 0) {
        if (close(writeSide) == -1)
            printMsg(lvlError, format("cannot close write side of pipe"));
        writeSide = 0;
    }
}


/* A goal is a store expression that still has to be normalised. */
struct Goal
{
    /* The path of the store expression. */
    Path nePath;

    /* The store expression stored at nePath. */
    StoreExpr expr;
    
    /* The unfinished inputs are the input store expressions that
       still have to be normalised. */
    PathSet unfinishedInputs;

    /* The waiters are the store expressions that have this one as an
       unfinished input. */
    PathSet waiters;

    /* The remainder is state held during the build. */

    /* Locks on the output paths. */
    PathLocks outputLocks;

    /* Input paths, with their closure elements. */
    ClosureElems inClosures; 

    /* Referenceable paths (i.e., input and output paths). */
    PathSet allPaths;

    /* The process ID of the builder. */
    pid_t pid;

    /* The temporary directory. */
    Path tmpDir;

    /* File descriptor for the log file. */
    int fdLogFile;

    /* Pipe for the builder's standard output/error. */
    Pipe logPipe;

    /* Pipes for talking to the build hook (if any). */
    Pipe toHook;
    Pipe fromHook;

    /* !!! clean up */
    PathSet fnord;
    map<Path, Path> xyzzy;

    Goal();
    ~Goal();

    void deleteTmpDir(bool force);
};


Goal::Goal()
    : pid(0)
    , tmpDir("")
    , fdLogFile(0)
{
}


Goal::~Goal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */

    if (pid) {
        printMsg(lvlError, format("killing child process %1% (%2%)")
            % pid % nePath);

        /* Send a KILL signal to every process in the child
           process group (which hopefully includes *all* its
           children). */
        if (kill(-pid, SIGKILL) != 0)
            printMsg(lvlError, format("killing process %1%") % pid);
        else {
            /* Wait until the child dies, disregarding the exit
               status. */
            int status;
            while (waitpid(pid, &status, 0) == -1)
                if (errno != EINTR) printMsg(lvlError,
                    format("waiting for process %1%") % pid);
        }
    }

    if (fdLogFile && (close(fdLogFile) != 0))
        printMsg(lvlError, format("cannot close fd"));

    try {
        deleteTmpDir(false);
    } catch (Error & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.msg());
    }
}


void Goal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        if (keepFailed && !force)
	    printMsg(lvlTalkative, 
		format("builder for `%1%' failed; keeping build directory `%2%'")
                % nePath % tmpDir);
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


/* A set of goals keyed on the path of the store expression. */
typedef map<Path, Goal> Goals;


/* A mapping used to remember for each child process what derivation
   store expression it is building. */
typedef map<pid_t, Path> Building;


/* The normaliser class. */
class Normaliser
{
private:
    /* The goals of the normaliser.  This describes a dependency graph
       of derivation expressions that have yet to be normalised. */
    Goals goals;

    /* The set of `buildable' goals, which are the ones with an empty
       list of unfinished inputs. */
    PathSet buildable;

    /* Child processes currently running. */
    Building building;

public:
    Normaliser();

    /* Add the normalisation of a store expression of a goal.  Returns
       true if the expression has been added; false if it's
       unnecessary (the expression is a closure, or already has a
       known successor). */
    bool addGoal(Path nePath);

    /* Perform build actions until all goals have been realised. */
    void run();

private:

    bool canBuildMore();
    
    /* Start building a derivation.  Returns false if we decline to
       build it right now. */
    bool startBuild(Path nePath);

    /* Acquires locks on the output paths and gathers information
       about the build (e.g., the input closures).  During this
       process its possible that we find out that the build is
       unnecessary, in which case we return false (this is not an
       error condition!). */
    bool prepareBuild(Goal & goal);

    void startBuildChild(Goal & goal);

    bool tryBuildHook(Goal & goal);

    void openLogFile(Goal & goal);
    
    void initChild(Goal & goal);

    void childStarted(Goal & goal, pid_t pid);

    /* Read from the logger pipes, and watch for child termination as
       a side effect.  Return true when a child terminates, false
       otherwise. */
    bool waitForChildren();

    /* Wait for child processes to finish building a derivation. */
    void reapChild(Goal & goal);

    /* Called when a build has finished succesfully. */
    void finishGoal(Goal & goal);

    /* Removes a goal from the graph and wakes up all waiters. */
    void removeGoal(Goal & goal);
};


static Path useSuccessor(const Path & path)
{
    string pathSucc;
    if (querySuccessor(path, pathSucc)) {
        debug(format("successor %1% -> %2%") % (string) path % pathSucc);
        return pathSucc;
    } else
        return path;
}


Normaliser::Normaliser()
{
}


bool Normaliser::addGoal(Path nePath)
{
    checkInterrupt();

    Goal goal;
    goal.nePath = nePath;

    /* If this already a goal, return. */
    if (goals.find(nePath) != goals.end()) return true;

    /* If we already have a successor, then we are done already; don't
       add the expression as a goal. */
    Path nfPath;
    if (querySuccessor(nePath, nfPath)) return false;

    /* Get the store expression. */
    goal.expr = storeExprFromPath(nePath);

    /* If this is a normal form (i.e., a closure) we are also done. */
    if (goal.expr.type == StoreExpr::neClosure) return false;
    if (goal.expr.type != StoreExpr::neDerivation) abort();

    /* Otherwise, it's a derivation expression for which the successor
       is not known, and we have to build it to determine its normal
       form.  So add it as a goal. */
    startNest(nest, lvlChatty,
        format("adding build goal `%1%'") % nePath);

    /* Inputs may also need to be added as goals if they haven't been
       normalised yet. */
    for (PathSet::iterator i = goal.expr.derivation.inputs.begin();
         i != goal.expr.derivation.inputs.end(); ++i)
        if (addGoal(*i)) {
            goal.unfinishedInputs.insert(*i);
            goals[*i].waiters.insert(nePath);
        }

    /* Maintain the invariant that all goals with no unfinished inputs
       are in the `buildable' set. */
    if (goal.unfinishedInputs.empty())
        buildable.insert(nePath);

    /* Add the goal to the goal graph. */
    goals[nePath] = goal;

    return true;
}


void Normaliser::run()
{
    startNest(nest, lvlChatty, format("running normaliser"));

    while (!goals.empty()) {

        debug("main loop - starting jobs");
        
        /* Start building as many buildable goals as possible. */
        bool madeProgress = false;
        
        for (PathSet::iterator i = buildable.begin();
             i != buildable.end(); ++i)
            
            if (startBuild(*i)) {
                madeProgress = true;
                buildable.erase(*i);
            }

        /* Wait until any child finishes (which may allow us to build
           new goals). */
        if (building.empty())
            assert(madeProgress); /* shouldn't happen */
        else
            do {
                printMsg(lvlVomit, "waiting for children");
            } while (!waitForChildren());
    }

    assert(buildable.empty() && building.empty());
}


bool Normaliser::canBuildMore()
{
    return building.size() < maxBuildJobs;
}


bool Normaliser::startBuild(Path nePath)
{
    checkInterrupt();

    Goals::iterator goalIt = goals.find(nePath);
    assert(goalIt != goals.end());
    Goal & goal(goalIt->second);
    assert(goal.unfinishedInputs.empty());

    startNest(nest, lvlTalkative,
        format("starting normalisation of goal `%1%'") % nePath);
    
    /* Is the build hook willing to accept this job? */
    if (tryBuildHook(goal)) return true;

    if (!canBuildMore()) {
        debug("postponing build");
        return false;
    }
    
    /* Prepare the build, i.e., acquire locks and gather necessary
       information. */
    if (!prepareBuild(goal)) return true;
    
    /* Otherwise, start the build in a child process. */
    startBuildChild(goal);

    return true;
}


bool Normaliser::prepareBuild(Goal & goal)
{
    /* The outputs are referenceable paths. */
    for (PathSet::iterator i = goal.expr.derivation.outputs.begin();
         i != goal.expr.derivation.outputs.end(); ++i)
    {
        debug(format("building path `%1%'") % *i);
        goal.allPaths.insert(*i);
    }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    /* !!! BUG: this could block, which is not allowed. */
    goal.outputLocks.lockPaths(goal.expr.derivation.outputs);

    /* Now check again whether there is a successor.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first successor
       check can be omitted, but that would be less efficient.)  Note
       that since we now hold the locks on the output paths, no other
       process can build this expression, so no further checks are
       necessary. */
    Path nfPath;
    if (querySuccessor(goal.nePath, nfPath)) {
        debug(format("skipping build of expression `%1%', someone beat us to it")
            % goal.nePath);
        goal.outputLocks.setDeletion(true);
        removeGoal(goal);
        return false;
    }

    /* Realise inputs (and remember all input paths). */
    for (PathSet::iterator i = goal.expr.derivation.inputs.begin();
         i != goal.expr.derivation.inputs.end(); ++i)
    {
        checkInterrupt();
        Path nfPath = useSuccessor(*i);
        realiseClosure(nfPath);
        goal.fnord.insert(nfPath);
        if (nfPath != *i) goal.xyzzy[*i] = nfPath;
        /* !!! nfPath should be a root of the garbage collector while
           we are building */
        StoreExpr ne = storeExprFromPath(nfPath);
        if (ne.type != StoreExpr::neClosure) abort();
        for (ClosureElems::iterator j = ne.closure.elems.begin();
             j != ne.closure.elems.end(); ++j)
	{
            goal.inClosures[j->first] = j->second;
	    goal.allPaths.insert(j->first);
	}
    }

    /* We can skip running the builder if all output paths are already
       valid. */
    bool fastBuild = true;
    for (PathSet::iterator i = goal.expr.derivation.outputs.begin();
         i != goal.expr.derivation.outputs.end(); ++i)
    {
        if (!isValidPath(*i)) { 
            fastBuild = false;
            break;
        }
    }

    if (fastBuild) {
        printMsg(lvlChatty, format("skipping build; output paths already exist"));
        finishGoal(goal);
        return false;
    }

    return true;
}


void Normaliser::startBuildChild(Goal & goal)
{
    /* Right platform? */
    if (goal.expr.derivation.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
		    % goal.expr.derivation.platform % thisSystem);

    /* If any of the outputs already exist but are not registered,
       delete them. */
    for (PathSet::iterator i = goal.expr.derivation.outputs.begin(); 
         i != goal.expr.derivation.outputs.end(); ++i)
    {
        Path path = *i;
        if (isValidPath(path))
            throw Error(format("obstructed build: path `%1%' exists") % path);
        if (pathExists(path)) {
            debug(format("removing unregistered path `%1%'") % path);
            deletePath(path);
        }
    }

    /* Construct the environment passed to the builder. */
    typedef map<string, string> Environment;
    Environment env; 
    
    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = "/homeless-shelter";

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = nixStore;

    /* Add all bindings specified in the derivation expression. */
    for (StringPairs::iterator i = goal.expr.derivation.env.begin();
         i != goal.expr.derivation.env.end(); ++i)
        env[i->first] = i->second;

    /* Create a temporary directory where the build will take
       place. */
    goal.tmpDir = createTempDir();

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = goal.tmpDir;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = goal.tmpDir;

    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder `%1%'") %
        goal.expr.derivation.builder);

    /* Create the log file and pipe. */
    openLogFile(goal);
    
    /* Fork a child to build the package.  Note that while we
       currently use forks to run and wait for the children, it
       shouldn't be hard to use threads for this on systems where
       fork() is unavailable or inefficient. */
    pid_t pid;
    switch (pid = fork()) {

    case -1:
        throw SysError("unable to fork");

    case 0:

        /* Warning: in the child we should absolutely not make any
           Berkeley DB calls! */

        try { /* child */

            initChild(goal);

            /* Fill in the arguments. */
            Strings & args(goal.expr.derivation.args);
            const char * argArr[args.size() + 2];
            const char * * p = argArr;
            string progName = baseNameOf(goal.expr.derivation.builder);
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

            /* Execute the program.  This should not return. */
            execve(goal.expr.derivation.builder.c_str(),
                (char * *) argArr, (char * *) envArr);

            throw SysError(format("executing `%1%'")
                % goal.expr.derivation.builder);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
        }
        _exit(1);
    }

    /* parent */

    childStarted(goal, pid);
}


string readLine(int fd)
{
    string s;
    while (1) {
        char ch;
        int rd = read(fd, &ch, 1);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading a line");
        } else if (rd == 0)
            throw Error("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}


bool Normaliser::tryBuildHook(Goal & goal)
{
    Path buildHook = getEnv("NIX_BUILD_HOOK");
    if (buildHook == "") return false;
    buildHook = absPath(buildHook);

    /* Create a directory where we will store files used for
       communication between us and the build hook. */
    goal.tmpDir = createTempDir();
    
    /* Create the log file and pipe. */
    openLogFile(goal);

    /* Create the communication pipes. */
    goal.toHook.create();
    goal.fromHook.create();

    /* Fork the hook. */
    pid_t pid;
    switch (pid = fork()) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            initChild(goal);

            execl(buildHook.c_str(), buildHook.c_str(),
                (canBuildMore() ? (string) "1" : "0").c_str(),
                thisSystem.c_str(),
                goal.expr.derivation.platform.c_str(),
                goal.nePath.c_str());
            
            throw SysError(format("executing `%1%'") % buildHook);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
        }
        _exit(1);
    }
    
    /* parent */

    childStarted(goal, pid);

    goal.fromHook.closeWriteSide();
    goal.toHook.closeReadSide();

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build.  !!! potential
       for deadlock here: we should also read from the child's logger
       pipe. */
    string reply = readLine(goal.fromHook.readSide);

    debug(format("hook reply is `%1%'") % reply);

    if (reply == "decline" || reply == "postpone") {
        /* Clean up the child.  !!! hacky / should verify */
        /* !!! drain stdout of hook, wait for child process */
        goal.pid = 0;
        goal.fromHook.closeReadSide();
        goal.toHook.closeWriteSide();
        close(goal.fdLogFile);
        goal.fdLogFile = 0;
        goal.logPipe.closeReadSide();
        building.erase(pid);
        return reply == "postpone";
    }

    else if (reply == "accept") {

        if (!prepareBuild(goal))
            throw Error("NOT IMPLEMENTED: hook unnecessary");

        Path inputListFN = goal.tmpDir + "/inputs";
        Path outputListFN = goal.tmpDir + "/outputs";
        Path successorsListFN = goal.tmpDir + "/successors";

        string s;
        for (ClosureElems::iterator i = goal.inClosures.begin();
             i != goal.inClosures.end(); ++i)
            s += i->first + "\n";
        for (PathSet::iterator i = goal.fnord.begin();
             i != goal.fnord.end(); ++i)
            s += *i + "\n";
        writeStringToFile(inputListFN, s);
        
        s = "";
        for (PathSet::iterator i = goal.expr.derivation.outputs.begin();
             i != goal.expr.derivation.outputs.end(); ++i)
            s += *i + "\n";
        writeStringToFile(outputListFN, s);
        
        s = "";
        for (map<Path, Path>::iterator i = goal.xyzzy.begin();
             i != goal.xyzzy.end(); ++i)
            s += i->first + " " + i->second + "\n";
        writeStringToFile(successorsListFN, s);

        string okay = "okay\n";
        writeFull(goal.toHook.writeSide,
            (const unsigned char *) okay.c_str(), okay.size());

        return true;
    }

    else throw Error(format("bad hook reply `%1%'") % reply);
}


void Normaliser::openLogFile(Goal & goal)
{
    /* Create a log file. */
    Path logFileName = nixLogDir + "/" + baseNameOf(goal.nePath);
    int fdLogFile = open(logFileName.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fdLogFile == -1)
        throw SysError(format("creating log file `%1%'") % logFileName);
    goal.fdLogFile = fdLogFile;

    /* Create a pipe to get the output of the child. */
    goal.logPipe.create();
}


void Normaliser::initChild(Goal & goal)
{
    /* Put the child in a separate process group so that it doesn't
       receive terminal signals. */
    if (setpgrp() == -1)
        throw SysError(format("setting process group"));

    if (chdir(goal.tmpDir.c_str()) == -1)
        throw SysError(format("changing into to `%1%'") % goal.tmpDir);

    /* Dup the write side of the logger pipe into stderr. */
    if (dup2(goal.logPipe.writeSide, STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");
    if (close(goal.logPipe.readSide) != 0) /* close read side */
        throw SysError("closing fd");
            
    /* Dup stderr to stdin. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError(format("cannot open `%1%'") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");

    /* When running a hook, dup the communication pipes. */
    bool inHook = goal.fromHook.writeSide != 0;
    if (inHook) {
        goal.fromHook.closeReadSide();
        if (dup2(goal.fromHook.writeSide, 3) == -1)
            throw SysError("dup");

        goal.toHook.closeWriteSide();
        if (dup2(goal.toHook.readSide, 4) == -1)
            throw SysError("dup");
    }

    /* Close all other file descriptors. */
    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO
            && (!inHook || (fd != 3 && fd != 4)))
            close(fd); /* ignore result */
}


void Normaliser::childStarted(Goal & goal, pid_t pid)
{
    goal.pid = pid;
    
    building[goal.pid] = goal.nePath;

    /* Close the write side of the logger pipe. */
    goal.logPipe.closeWriteSide();
}


bool Normaliser::waitForChildren()
{
    checkInterrupt();

    bool terminated = false;
    
    /* Process log output from the children.  We also use this to
       detect child termination: if we get EOF on the logger pipe of a
       build, we assume that the builder has terminated. */

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    for (Building::iterator i = building.begin();
         i != building.end(); ++i)
    {
        Goal & goal(goals[i->second]);
        int fd = goal.logPipe.readSide;
        FD_SET(fd, &fds);
        if (fd >= fdMax) fdMax = fd + 1;
    }

    if (select(fdMax, &fds, 0, 0, 0) == -1) {
        if (errno == EINTR) return false;
        throw SysError("waiting for input");
    }

    /* Process all available file descriptors. */
    for (Building::iterator i = building.begin();
         i != building.end(); ++i)
    {
        checkInterrupt();
        Goal & goal(goals[i->second]);
        int fd = goal.logPipe.readSide;
        if (FD_ISSET(fd, &fds)) {
            unsigned char buffer[1024];
            ssize_t rd = read(fd, buffer, sizeof(buffer));
            if (rd == -1) {
                if (errno != EINTR)
                    throw SysError(format("reading from `%1%'")
                        % goal.nePath);
            } else if (rd == 0) {
                debug(format("EOF on `%1%'") % goal.nePath);
                reapChild(goal);
                terminated = true;
            } else {
                printMsg(lvlVomit, format("read %1% bytes from `%2%'")
                    % rd % goal.nePath);
                writeFull(goal.fdLogFile, buffer, rd);
                if (verbosity >= buildVerbosity)
                    writeFull(STDERR_FILENO, buffer, rd);
            }
        }
    }

    return terminated;
}


void Normaliser::reapChild(Goal & goal)
{
    int status;

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe --- just don't do that
       :-) */
    /* !!! this could block! */
    if (waitpid(goal.pid, &status, 0) != goal.pid)
        throw SysError(format("builder for `%1%' should have terminated")
            % goal.nePath);

    /* So the child is gone now. */
    pid_t pid = goal.pid;
    goal.pid = 0;

    /* Close the read side of the logger pipe. */
    goal.logPipe.closeReadSide();

    /* Close the log file. */
    if (close(goal.fdLogFile) != 0)
        throw SysError("closing fd");
    goal.fdLogFile = 0;

    debug(format("builder process %1% finished") % pid);

    /* Check the exit status. */
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        goal.deleteTmpDir(false);
        if (WIFEXITED(status))
            throw Error(format("builder for `%1%' failed with exit code %2%")
                % goal.nePath % WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            throw Error(format("builder for `%1%' failed due to signal %2%")
                % goal.nePath % WTERMSIG(status));
        else
            throw Error(format("builder for `%1%' failed died abnormally") % goal.nePath);
    } else
        goal.deleteTmpDir(true);

    finishGoal(goal);
    
    building.erase(pid);
}


void Normaliser::finishGoal(Goal & goal)
{
    /* The resulting closure expression. */
    StoreExpr nf;
    nf.type = StoreExpr::neClosure;
    
    startNest(nest, lvlTalkative,
        format("finishing normalisation of goal `%1%'") % goal.nePath);
    
    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    PathSet usedPaths;
    for (PathSet::iterator i = goal.expr.derivation.outputs.begin(); 
         i != goal.expr.derivation.outputs.end(); ++i)
    {
        Path path = *i;
        if (!pathExists(path))
            throw Error(format("output path `%1%' does not exist") % path);
        nf.closure.roots.insert(path);

	makePathReadOnly(path);

	/* For this output path, find the references to other paths contained
	   in it. */
        startNest(nest2, lvlChatty,
            format("scanning for store references in `%1%'") % path);
        Strings refPaths = filterReferences(path, 
            Strings(goal.allPaths.begin(), goal.allPaths.end()));
        nest2.close();

	/* Construct a closure element for this output path. */
        ClosureElem elem;

	/* For each path referenced by this output path, add its id to the
	   closure element and add the id to the `usedPaths' set (so that the
	   elements referenced by *its* closure are added below). */
        for (Paths::iterator j = refPaths.begin();
	     j != refPaths.end(); ++j)
	{
            checkInterrupt();
	    Path path = *j;
	    elem.refs.insert(path);
            if (goal.inClosures.find(path) != goal.inClosures.end())
                usedPaths.insert(path);
	    else if (goal.expr.derivation.outputs.find(path) ==
                goal.expr.derivation.outputs.end())
		abort();
        }

        nf.closure.elems[path] = elem;
    }

    /* Close the closure.  That is, for any referenced path, add the paths
       referenced by it. */
    PathSet donePaths;

    while (!usedPaths.empty()) {
        checkInterrupt();
	PathSet::iterator i = usedPaths.begin();
	Path path = *i;
	usedPaths.erase(i);

	if (donePaths.find(path) != donePaths.end()) continue;
	donePaths.insert(path);

	ClosureElems::iterator j = goal.inClosures.find(path);
	if (j == goal.inClosures.end()) abort();

	nf.closure.elems[path] = j->second;

	for (PathSet::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    usedPaths.insert(*k);
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (ClosureElems::iterator i = goal.inClosures.begin();
         i != goal.inClosures.end(); ++i)
    {
        PathSet::iterator j = donePaths.find(i->first);
        if (j == donePaths.end())
            debug(format("unreferenced input: `%1%'") % i->first);
        else
            debug(format("referenced input: `%1%'") % i->first);
    }

    /* Write the normal form.  This does not have to occur in the
       transaction below because writing terms is idem-potent. */
    ATerm nfTerm = unparseStoreExpr(nf);
    printMsg(lvlVomit, format("normal form: %1%") % atPrint(nfTerm));
    Path nfPath = writeTerm(nfTerm, "-s");

    /* Register each output path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn;
    createStoreTransaction(txn);
    for (PathSet::iterator i = goal.expr.derivation.outputs.begin(); 
         i != goal.expr.derivation.outputs.end(); ++i)
        registerValidPath(txn, *i);
    registerSuccessor(txn, goal.nePath, nfPath);
    txn.commit();

    /* It is now safe to delete the lock files, since all future
       lockers will see the successor; they will not create new lock
       files with the same names as the old (unlinked) lock files. */
    goal.outputLocks.setDeletion(true);

    removeGoal(goal);
}


void Normaliser::removeGoal(Goal & goal)
{
    /* Remove this goal from those goals to which it is an input. */ 
    for (PathSet::iterator i = goal.waiters.begin();
         i != goal.waiters.end(); ++i)
    {
        Goal & waiter(goals[*i]);
        PathSet::iterator j = waiter.unfinishedInputs.find(goal.nePath);
        assert(j != waiter.unfinishedInputs.end());
        waiter.unfinishedInputs.erase(j);

        /* If there are not inputs left, the goal has become
           buildable. */ 
        if (waiter.unfinishedInputs.empty()) {
            debug(format("waking up goal `%1%'") % waiter.nePath);
            buildable.insert(waiter.nePath);
        }
    }

    /* Remove this goal from the graph.  Careful: after this `goal' is
       probably no longer valid. */
    goals.erase(goal.nePath);
}


Path normaliseStoreExpr(const Path & nePath)
{
    Normaliser normaliser;
    normaliser.addGoal(nePath);
    normaliser.run();

    Path nfPath;
    if (!querySuccessor(nePath, nfPath)) abort();
    
    return nfPath;
}


void realiseClosure(const Path & nePath)
{
    startNest(nest, lvlDebug, format("realising closure `%1%'") % nePath);

    StoreExpr ne = storeExprFromPath(nePath);
    if (ne.type != StoreExpr::neClosure)
        throw Error(format("expected closure in `%1%'") % nePath);
    
    for (ClosureElems::const_iterator i = ne.closure.elems.begin();
         i != ne.closure.elems.end(); ++i)
        ensurePath(i->first);
}


void ensurePath(const Path & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

#if 0
    if (pending.find(path) != pending.end())
      throw Error(format(
          "path `%1%' already being realised (possible substitute cycle?)")
	  % path);
    pending.insert(path);
    
    /* Otherwise, try the substitutes. */
    Paths subPaths = querySubstitutes(path);

    for (Paths::iterator i = subPaths.begin(); 
         i != subPaths.end(); ++i)
    {
        checkInterrupt();
        try {
            Path nf = normaliseStoreExpr(*i, pending);
	    realiseClosure(nf, pending);
            if (isValidPath(path)) return;
            throw Error(format("substitute failed to produce expected output path"));
        } catch (Error & e) {
            printMsg(lvlTalkative, 
                format("building of substitute `%1%' for `%2%' failed: %3%")
                % *i % path % e.what());
        }
    }
#endif

    throw Error(format("path `%1%' is required, "
        "but there are no (successful) substitutes") % path);
}


StoreExpr storeExprFromPath(const Path & path)
{
    assertStorePath(path);
    ensurePath(path);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return parseStoreExpr(t);
}


PathSet storeExprRoots(const Path & nePath)
{
    PathSet paths;

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        paths.insert(ne.closure.roots.begin(), ne.closure.roots.end());
    else if (ne.type == StoreExpr::neDerivation)
        paths.insert(ne.derivation.outputs.begin(),
            ne.derivation.outputs.end());
    else abort();

    return paths;
}


static void requisitesWorker(const Path & nePath,
    bool includeExprs, bool includeSuccessors,
    PathSet & paths, PathSet & doneSet)
{
    checkInterrupt();
    
    if (doneSet.find(nePath) != doneSet.end()) return;
    doneSet.insert(nePath);

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        for (ClosureElems::iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); ++i)
            paths.insert(i->first);
    
    else if (ne.type == StoreExpr::neDerivation)
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); ++i)
            requisitesWorker(*i,
                includeExprs, includeSuccessors, paths, doneSet);

    else abort();

    if (includeExprs) paths.insert(nePath);

    string nfPath;
    if (includeSuccessors && (nfPath = useSuccessor(nePath)) != nePath)
        requisitesWorker(nfPath, includeExprs, includeSuccessors,
            paths, doneSet);
}


PathSet storeExprRequisites(const Path & nePath,
    bool includeExprs, bool includeSuccessors)
{
    PathSet paths;
    PathSet doneSet;
    requisitesWorker(nePath, includeExprs, includeSuccessors,
        paths, doneSet);
    return paths;
}
