#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "normalise.hh"
#include "references.hh"
#include "pathlocks.hh"
#include "globals.hh"


/* !!! TODO storeExprFromPath shouldn't be used here */


static string pathNullDevice = "/dev/null";


/* Forward definition. */
class Worker;


/* A pointer to a goal. */
class Goal;
typedef shared_ptr<Goal> GoalPtr;

/* A set of goals. */
typedef set<GoalPtr> Goals;

/* A map of paths to goals (and the other way around). */
typedef map<Path, GoalPtr> GoalMap;
typedef map<GoalPtr, Path> GoalMapRev;



class Goal : public enable_shared_from_this<Goal>
{
protected:
    
    /* Backlink to the worker. */
    Worker & worker;

    /* Goals waiting for this one to finish. */
    Goals waiters;

    /* Number of goals we are waiting for. */
    unsigned int nrWaitees;
    

    Goal(Worker & _worker) : worker(_worker)
    {
        nrWaitees = 0;
    }

    virtual ~Goal()
    {
        printMsg(lvlVomit, "goal destroyed");
    }

public:
    virtual void work() = 0;

    virtual string name()
    {
        return "(noname)";
    }

    void addWaiter(GoalPtr waiter);

    void waiteeDone();

    void amDone();
};


/* A mapping used to remember for each child process to what goal it
   belongs, and a file descriptor for receiving log data. */
struct Child
{
    GoalPtr goal;
    int fdOutput;
    bool inBuildSlot;
};

typedef map<pid_t, Child> Children;


/* The worker class. */
class Worker
{
private:

    /* The goals of the worker. */
    Goals goals;

    /* Goals that are ready to do some work. */
    Goals awake;

    /* Goals waiting for a build slot. */
    Goals wantingToBuild;

    /* Child processes currently running. */
    Children children;

    /* Number of build slots occupied.  Not all child processes
       (namely build hooks) count as occupied build slots. */
    unsigned int nrChildren;

    /* Maps used to prevent multiple instantiation of a goal for the
       same expression / path. */
    GoalMap normalisationGoals;
    GoalMapRev normalisationGoalsRev;
    GoalMap realisationGoals;
    GoalMapRev realisationGoalsRev;
    GoalMap substitutionGoals;
    GoalMapRev substitutionGoalsRev;

public:

    Worker();
    ~Worker();

    /* Add a goal. */
    void addNormalisationGoal(const Path & nePath, GoalPtr waiter);
    void addRealisationGoal(const Path & nePath, GoalPtr waiter);
    void addSubstitutionGoal(const Path & storePath, GoalPtr waiter);

    /* Remove a finished goal. */
    void removeGoal(GoalPtr goal);

    /* Wake up a goal (i.e., there is something for it to do). */
    void wakeUp(GoalPtr goal);

    /* Can we start another child process? */
    bool canBuildMore();

    /* Registers / unregisters a running child process. */
    void childStarted(GoalPtr goal, pid_t pid, int fdOutput,
        bool inBuildSlot);
    void childTerminated(pid_t pid);

    /* Add a goal to the set of goals waiting for a build slot. */
    void waitForBuildSlot(GoalPtr goal);
    
    /* Loop until all goals have been realised. */
    void run();

    /* Wait for input to become available. */
    void waitForInput();
};



//////////////////////////////////////////////////////////////////////


void Goal::addWaiter(GoalPtr waiter)
{
    waiters.insert(waiter);
}


void Goal::waiteeDone()
{
    assert(nrWaitees > 0);
    if (!--nrWaitees) worker.wakeUp(shared_from_this());
}


void Goal::amDone()
{
    printMsg(lvlVomit, "done");
    for (Goals::iterator i = waiters.begin(); i != waiters.end(); ++i)
        (*i)->waiteeDone();
    worker.removeGoal(shared_from_this());
}



//////////////////////////////////////////////////////////////////////


/* Common initialisation performed in child processes. */
void commonChildInit(Pipe & logPipe)
{
    /* Put the child in a separate process group so that it doesn't
       receive terminal signals. */
    if (setpgid(0, 0) == -1)
        throw SysError(format("setting process group"));

    /* Dup the write side of the logger pipe into stderr. */
    if (dup2(logPipe.writeSide, STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");
    logPipe.readSide.close();
            
    /* Dup stderr to stdin. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError(format("cannot open `%1%'") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
}


const char * * strings2CharPtrs(const Strings & ss)
{
    const char * * arr = new const char * [ss.size() + 1];
    const char * * p = arr;
    for (Strings::const_iterator i = ss.begin(); i != ss.end(); ++i)
        *p++ = i->c_str();
    *p = 0;
    return arr;
}



//////////////////////////////////////////////////////////////////////


class NormalisationGoal : public Goal
{
private:
    /* The path of the derivation store expression. */
    Path nePath;

    /* The store expression stored at nePath. */
    StoreExpr expr;
    
    /* The remainder is state held during the build. */

    /* Locks on the output paths. */
    PathLocks outputLocks;

    /* Input paths, with their closure elements. */
    ClosureElems inClosures; 

    /* Referenceable paths (i.e., input and output paths). */
    PathSet allPaths;

    /* The normal forms of the input store expressions. */
    PathSet inputNFs;

    /* The successor mappings for the input store expressions. */
    map<Path, Path> inputSucs;

    /* The process ID of the builder. */
    Pid pid;

    /* The temporary directory. */
    Path tmpDir;

    /* File descriptor for the log file. */
    AutoCloseFD fdLogFile;

    /* Pipe for the builder's standard output/error. */
    Pipe logPipe;

    /* Pipes for talking to the build hook (if any). */
    Pipe toHook;
    Pipe fromHook;

    typedef void (NormalisationGoal::*GoalState)();
    GoalState state;
    
public:
    NormalisationGoal(const Path & _nePath, Worker & _worker);
    ~NormalisationGoal();

    void work();

private:
    /* The states. */
    void init();
    void haveStoreExpr();
    void inputNormalised();
    void inputRealised();
    void tryToBuild();
    void buildDone();

    /* Is the build hook willing to perform the build? */
    typedef enum {rpAccept, rpDecline, rpPostpone, rpDone} HookReply;
    HookReply tryBuildHook();

    /* Synchronously wait for a build hook to finish. */
    void terminateBuildHook();

    /* Acquires locks on the output paths and gathers information
       about the build (e.g., the input closures).  During this
       process its possible that we find out that the build is
       unnecessary, in which case we return false (this is not an
       error condition!). */
    bool prepareBuild();

    /* Start building a derivation. */
    void startBuilder();

    /* Must be called after the output paths have become valid (either
       due to a successful build or hook, or because they already
       were). */
    void createClosure();

    /* Open a log file and a pipe to it. */
    void openLogFile();

    /* Common initialisation to be performed in child processes (i.e.,
       both in builders and in build hooks. */
    void initChild();
    
    /* Delete the temporary directory, if we have one. */
    void deleteTmpDir(bool force);

    string name()
    {
        return nePath;
    }

    void trace(const format & f);
};


NormalisationGoal::NormalisationGoal(const Path & _nePath, Worker & _worker)
    : Goal(_worker)
{
    nePath = _nePath;
    state = &NormalisationGoal::init;
}


NormalisationGoal::~NormalisationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try {
        deleteTmpDir(false);
    } catch (Error & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.msg());
    }
}


void NormalisationGoal::work()
{
    (this->*state)();
}


void NormalisationGoal::init()
{
    trace("init");

    /* If we already have a successor, then we are done already; don't
       add the expression as a goal. */
    Path nfPath;
    if (querySuccessor(nePath, nfPath)) {
        amDone();
        return;
    }

    /* The first thing to do is to make sure that the store expression
       exists.  If it doesn't, it may be created through a
       substitute. */
    nrWaitees = 1;
    worker.addSubstitutionGoal(nePath, shared_from_this());

    state = &NormalisationGoal::haveStoreExpr;
}


void NormalisationGoal::haveStoreExpr()
{
    trace("loading store expression");

    assert(isValidPath(nePath));

    /* Get the store expression. */
    expr = storeExprFromPath(nePath);

    /* If this is a normal form (i.e., a closure) we are also done. */
    if (expr.type == StoreExpr::neClosure) {
        amDone();
        return;
    }
    assert(expr.type == StoreExpr::neDerivation);

    /* Inputs must be normalised before we can build this goal. */
    for (PathSet::iterator i = expr.derivation.inputs.begin();
         i != expr.derivation.inputs.end(); ++i)
        worker.addNormalisationGoal(*i, shared_from_this());

    nrWaitees = expr.derivation.inputs.size();

    state = &NormalisationGoal::inputNormalised;
}


void NormalisationGoal::inputNormalised()
{
    trace("all inputs normalised");

    /* Inputs must also be realised before we can build this goal. */
    for (PathSet::iterator i = expr.derivation.inputs.begin();
         i != expr.derivation.inputs.end(); ++i)
    {
        Path neInput = *i, nfInput;
        if (querySuccessor(neInput, nfInput))
            neInput = nfInput;
        /* Otherwise the input must be a closure. */
        worker.addRealisationGoal(neInput, shared_from_this());
    }
    
    nrWaitees = expr.derivation.inputs.size();

    state = &NormalisationGoal::inputRealised;
}


void NormalisationGoal::inputRealised()
{
    trace("all inputs realised");

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &NormalisationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());
}


void NormalisationGoal::tryToBuild()
{
    trace("trying to build");

    /* Is the build hook willing to accept this job? */
    switch (tryBuildHook()) {
        case rpAccept:
            /* Yes, it has started doing so.  Wait until we get
               EOF from the hook. */
            state = &NormalisationGoal::buildDone;
            return;
        case rpPostpone:
            /* Not now; wait until at least one child finishes. */
            worker.waitForBuildSlot(shared_from_this());
            return;
        case rpDecline:
            /* We should do it ourselves. */
            break;
        case rpDone:
            /* Somebody else did it (there is a successor now). */
            amDone();
            return;
    }

    /* Make sure that we are allowed to start a build. */
    if (!worker.canBuildMore()) {
        worker.waitForBuildSlot(shared_from_this());
        return;
    }

    /* Acquire locks and such.  If we then see that there now is a
       successor, we're done. */
    if (!prepareBuild()) {
        amDone();
        return;
    }

    /* Okay, we have to build. */
    startBuilder();

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &NormalisationGoal::buildDone;
}


void NormalisationGoal::buildDone()
{
    trace("build done");

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe --- just don't do that
       :-) */
    /* !!! this could block! */
    pid_t savedPid = pid;
    int status = pid.wait(true);

    /* So the child is gone now. */
    worker.childTerminated(savedPid);

    /* Close the read side of the logger pipe. */
    logPipe.readSide.close();

    /* Close the log file. */
    fdLogFile.close();

    debug(format("builder process for `%1%' finished") % nePath);

    /* Check the exit status. */
    if (!statusOk(status)) {
        deleteTmpDir(false);
        throw Error(format("builder for `%1%' %2%")
            % nePath % statusToString(status));
    }
    
    deleteTmpDir(true);

    /* Compute a closure store expression, and register it as our
       successor. */
    createClosure();

    amDone();
}


static string readLine(int fd)
{
    string s;
    while (1) {
        char ch;
        ssize_t rd = read(fd, &ch, 1);
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


static void writeLine(int fd, string s)
{
    s += '\n';
    writeFull(fd, (const unsigned char *) s.c_str(), s.size());
}


/* !!! ugly hack */
static void drain(int fd)
{
    unsigned char buffer[1024];
    while (1) {
        ssize_t rd = read(fd, buffer, sizeof buffer);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("draining");
        } else if (rd == 0) break;
        else writeFull(STDERR_FILENO, buffer, rd);
    }
}


NormalisationGoal::HookReply NormalisationGoal::tryBuildHook()
{
    Path buildHook = getEnv("NIX_BUILD_HOOK");
    if (buildHook == "") return rpDecline;
    buildHook = absPath(buildHook);

    /* Create a directory where we will store files used for
       communication between us and the build hook. */
    tmpDir = createTempDir();
    
    /* Create the log file and pipe. */
    openLogFile();

    /* Create the communication pipes. */
    toHook.create();
    fromHook.create();

    /* Fork the hook. */
    pid = fork();
    switch (pid) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            initChild();

            execl(buildHook.c_str(), buildHook.c_str(),
                (worker.canBuildMore() ? (string) "1" : "0").c_str(),
                thisSystem.c_str(),
                expr.derivation.platform.c_str(),
                nePath.c_str(), 0);
            
            throw SysError(format("executing `%1%'") % buildHook);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
        }
        _exit(1);
    }
    
    /* parent */
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, logPipe.readSide, false);

    fromHook.writeSide.close();
    toHook.readSide.close();

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build.  !!! potential
       for deadlock here: we should also read from the child's logger
       pipe. */
    string reply;
    try {
        reply = readLine(fromHook.readSide);
    } catch (Error & e) {
        drain(logPipe.readSide);
        throw;
    }

    debug(format("hook reply is `%1%'") % reply);

    if (reply == "decline" || reply == "postpone") {
        /* Clean up the child.  !!! hacky / should verify */
        drain(logPipe.readSide);
        terminateBuildHook();
        return reply == "decline" ? rpDecline : rpPostpone;
    }

    else if (reply == "accept") {

        /* Acquire locks and such.  If we then see that there now is a
           successor, we're done. */
        if (!prepareBuild()) {
            /* Tell the hook to exit. */
            writeLine(toHook.writeSide, "cancel");
            terminateBuildHook();
            return rpDone;
        }

        printMsg(lvlInfo, format("running hook to build path `%1%'")
            % *expr.derivation.outputs.begin());
        
        /* Write the information that the hook needs to perform the
           build, i.e., the set of input paths (including closure
           expressions), the set of output paths, and the successor
           mappings for the input expressions. */
        
        Path inputListFN = tmpDir + "/inputs";
        Path outputListFN = tmpDir + "/outputs";
        Path successorsListFN = tmpDir + "/successors";

        string s;
        for (ClosureElems::iterator i = inClosures.begin();
             i != inClosures.end(); ++i)
            s += i->first + "\n";
        for (PathSet::iterator i = inputNFs.begin();
             i != inputNFs.end(); ++i)
            s += *i + "\n";
        writeStringToFile(inputListFN, s);
        
        s = "";
        for (PathSet::iterator i = expr.derivation.outputs.begin();
             i != expr.derivation.outputs.end(); ++i)
            s += *i + "\n";
        writeStringToFile(outputListFN, s);
        
        s = "";
        for (map<Path, Path>::iterator i = inputSucs.begin();
             i != inputSucs.end(); ++i)
            s += i->first + " " + i->second + "\n";
        writeStringToFile(successorsListFN, s);

        /* Tell the hook to proceed. */ 
        writeLine(toHook.writeSide, "okay");

        return rpAccept;
    }

    else throw Error(format("bad hook reply `%1%'") % reply);
}


void NormalisationGoal::terminateBuildHook()
{
    /* !!! drain stdout of hook */
    debug("terminating build hook");
    pid_t savedPid = pid;
    pid.wait(true);
    worker.childTerminated(savedPid);
    fromHook.readSide.close();
    toHook.writeSide.close();
    fdLogFile.close();
    logPipe.readSide.close();
}


bool NormalisationGoal::prepareBuild()
{
    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    /* !!! BUG: this could block, which is not allowed. */
    outputLocks.lockPaths(expr.derivation.outputs);

    /* Now check again whether there is a successor.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first successor
       check can be omitted, but that would be less efficient.)  Note
       that since we now hold the locks on the output paths, no other
       process can build this expression, so no further checks are
       necessary. */
    Path nfPath;
    if (querySuccessor(nePath, nfPath)) {
        debug(format("skipping build of expression `%1%', someone beat us to it")
            % nePath);
        outputLocks.setDeletion(true);
        return false;
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */
    
    /* The outputs are referenceable paths. */
    for (PathSet::iterator i = expr.derivation.outputs.begin();
         i != expr.derivation.outputs.end(); ++i)
    {
        debug(format("building path `%1%'") % *i);
        allPaths.insert(*i);
    }
    
    /* Get information about the inputs (these all exist now). */
    for (PathSet::iterator i = expr.derivation.inputs.begin();
         i != expr.derivation.inputs.end(); ++i)
    {
        checkInterrupt();
        Path nePath = *i, nfPath;
        if (!querySuccessor(nePath, nfPath)) nfPath = nePath;
        inputNFs.insert(nfPath);
        if (nfPath != nePath) inputSucs[nePath] = nfPath;
        /* !!! nfPath should be a root of the garbage collector while
           we are building */
        StoreExpr ne = storeExprFromPath(nfPath);
        if (ne.type != StoreExpr::neClosure) abort();
        for (ClosureElems::iterator j = ne.closure.elems.begin();
             j != ne.closure.elems.end(); ++j)
	{
            inClosures[j->first] = j->second;
	    allPaths.insert(j->first);
	}
    }

    /* We can skip running the builder if all output paths are already
       valid. */
    bool fastBuild = true;
    for (PathSet::iterator i = expr.derivation.outputs.begin();
         i != expr.derivation.outputs.end(); ++i)
        if (!isValidPath(*i)) { 
            fastBuild = false;
            break;
        }

    if (fastBuild) {
        printMsg(lvlChatty, format("skipping build; output paths already exist"));
        createClosure();
        return false;
    }

    return true;
}


void NormalisationGoal::startBuilder()
{
    startNest(nest, lvlInfo,
        format("building path `%1%'") % *expr.derivation.outputs.begin());
    
    /* Right platform? */
    if (expr.derivation.platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
		    % expr.derivation.platform % thisSystem);

    /* If any of the outputs already exist but are not registered,
       delete them. */
    for (PathSet::iterator i = expr.derivation.outputs.begin(); 
         i != expr.derivation.outputs.end(); ++i)
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
    for (StringPairs::iterator i = expr.derivation.env.begin();
         i != expr.derivation.env.end(); ++i)
        env[i->first] = i->second;

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir();

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDir;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDir;

    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder `%1%'") %
        expr.derivation.builder);

    /* Create the log file and pipe. */
    openLogFile();
    
    /* Fork a child to build the package.  Note that while we
       currently use forks to run and wait for the children, it
       shouldn't be hard to use threads for this on systems where
       fork() is unavailable or inefficient. */
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0:

        /* Warning: in the child we should absolutely not make any
           Berkeley DB calls! */

        try { /* child */

            initChild();

            /* Fill in the arguments. */
            Strings args(expr.derivation.args);
            args.push_front(baseNameOf(expr.derivation.builder));
            const char * * argArr = strings2CharPtrs(args);

            /* Fill in the environment. */
            Strings envStrs;
            for (Environment::const_iterator i = env.begin();
                 i != env.end(); ++i)
                envStrs.push_back(i->first + "=" + i->second);
            const char * * envArr = strings2CharPtrs(envStrs);

            /* Execute the program.  This should not return. */
            execve(expr.derivation.builder.c_str(),
                (char * *) argArr, (char * *) envArr);

            throw SysError(format("executing `%1%'")
                % expr.derivation.builder);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
        }
        _exit(1);
    }

    /* parent */
    pid.setSeparatePG(true);
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, logPipe.readSide, true);
}


void NormalisationGoal::createClosure()
{
    /* The resulting closure expression. */
    StoreExpr nf;
    nf.type = StoreExpr::neClosure;
    
    startNest(nest, lvlTalkative,
        format("computing closure for `%1%'") % nePath);
    
    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    PathSet usedPaths;
    for (PathSet::iterator i = expr.derivation.outputs.begin(); 
         i != expr.derivation.outputs.end(); ++i)
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
            Strings(allPaths.begin(), allPaths.end()));
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
            if (inClosures.find(path) != inClosures.end())
                usedPaths.insert(path);
	    else if (expr.derivation.outputs.find(path) ==
                expr.derivation.outputs.end())
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

	ClosureElems::iterator j = inClosures.find(path);
	if (j == inClosures.end()) abort();

	nf.closure.elems[path] = j->second;

	for (PathSet::iterator k = j->second.refs.begin();
	     k != j->second.refs.end(); k++)
	    usedPaths.insert(*k);
    }

    /* For debugging, print out the referenced and unreferenced paths. */
    for (ClosureElems::iterator i = inClosures.begin();
         i != inClosures.end(); ++i)
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
    Path nfPath = writeTerm(nfTerm, "-s");

    /* Register each output path, and register the normal form.  This
       is wrapped in one database transaction to ensure that if we
       crash, either everything is registered or nothing is.  This is
       for recoverability: unregistered paths in the store can be
       deleted arbitrarily, while registered paths can only be deleted
       by running the garbage collector. */
    Transaction txn;
    createStoreTransaction(txn);
    for (PathSet::iterator i = expr.derivation.outputs.begin(); 
         i != expr.derivation.outputs.end(); ++i)
        registerValidPath(txn, *i);
    registerSuccessor(txn, nePath, nfPath);
    txn.commit();

    /* It is now safe to delete the lock files, since all future
       lockers will see the successor; they will not create new lock
       files with the same names as the old (unlinked) lock files. */
    outputLocks.setDeletion(true);
}


void NormalisationGoal::openLogFile()
{
    /* Create a log file. */
    Path logFileName = nixLogDir + "/" + baseNameOf(nePath);
    fdLogFile = open(logFileName.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fdLogFile == -1)
        throw SysError(format("creating log file `%1%'") % logFileName);

    /* Create a pipe to get the output of the child. */
    logPipe.create();
}


void NormalisationGoal::initChild()
{
    commonChildInit(logPipe);
    
    if (chdir(tmpDir.c_str()) == -1)
        throw SysError(format("changing into to `%1%'") % tmpDir);

    /* When running a hook, dup the communication pipes. */
    bool inHook = fromHook.writeSide.isOpen();
    if (inHook) {
        fromHook.readSide.close();
        if (dup2(fromHook.writeSide, 3) == -1)
            throw SysError("dup1");

        toHook.writeSide.close();
        if (dup2(toHook.readSide, 4) == -1)
            throw SysError("dup2");
    }

    /* Close all other file descriptors. */
    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO
            && (!inHook || (fd != 3 && fd != 4)))
            close(fd); /* ignore result */
}


void NormalisationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        if (keepFailed && !force)
	    printMsg(lvlError, 
		format("builder for `%1%' failed; keeping build directory `%2%'")
                % nePath % tmpDir);
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


void NormalisationGoal::trace(const format & f)
{
    debug(format("normalisation of `%1%': %2%") % nePath % f);
}



//////////////////////////////////////////////////////////////////////


class RealisationGoal : public Goal
{
private:
    /* The path of the closure store expression. */
    Path nePath;

    /* The store expression stored at nePath. */
    StoreExpr expr;
    
    typedef void (RealisationGoal::*GoalState)();
    GoalState state;
    
public:
    RealisationGoal(const Path & _nePath, Worker & _worker);
    ~RealisationGoal();

    void work();
    
    /* The states. */
    void init();
    void haveStoreExpr();
    void elemFinished();

    void trace(const format & f);
};


RealisationGoal::RealisationGoal(const Path & _nePath, Worker & _worker)
    : Goal(_worker)
{
    nePath = _nePath;
    state = &RealisationGoal::init;
}


RealisationGoal::~RealisationGoal()
{
}


void RealisationGoal::work()
{
    (this->*state)();
}


void RealisationGoal::init()
{
    trace("init");

    /* The first thing to do is to make sure that the store expression
       exists.  If it doesn't, it may be created through a
       substitute. */
    nrWaitees = 1;
    worker.addSubstitutionGoal(nePath, shared_from_this());

    state = &RealisationGoal::haveStoreExpr;
}


void RealisationGoal::haveStoreExpr()
{
    trace("loading store expression");

    assert(isValidPath(nePath));

    /* Get the store expression. */
    expr = storeExprFromPath(nePath);

    /* If this is a normal form (i.e., a closure) we are also done. */
    if (expr.type != StoreExpr::neClosure)
        throw Error(format("expected closure in `%1%'") % nePath);

    /* Each path in the closure should exist, or should be creatable
       through a substitute. */
    for (ClosureElems::const_iterator i = expr.closure.elems.begin();
         i != expr.closure.elems.end(); ++i)
        worker.addSubstitutionGoal(i->first, shared_from_this());
    
    nrWaitees = expr.closure.elems.size();

    state = &RealisationGoal::elemFinished;
}


void RealisationGoal::elemFinished()
{
    trace("all closure elements present");

    amDone();
}


void RealisationGoal::trace(const format & f)
{
    debug(format("realisation of `%1%': %2%") % nePath % f);
}



//////////////////////////////////////////////////////////////////////


class SubstitutionGoal : public Goal
{
private:
    /* The store path that should be realised through a substitute. */
    Path storePath;

    /* The remaining substitutes for this path. */
    Substitutes subs;

    /* The current substitute. */
    Substitute sub;

    /* The normal form of the substitute store expression. */
    Path nfSub;

    /* Pipe for the substitute's standard output/error. */
    Pipe logPipe;

    /* The process ID of the builder. */
    Pid pid;

    /* Lock on the store path. */
    PathLocks outputLock;
    
    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

public:
    SubstitutionGoal(const Path & _nePath, Worker & _worker);

    void work();

    /* The states. */
    void init();
    void tryNext();
    void exprNormalised();
    void exprRealised();
    void tryToRun();
    void finished();

    void trace(const format & f);
};


SubstitutionGoal::SubstitutionGoal(const Path & _storePath, Worker & _worker)
    : Goal(_worker)
{
    storePath = _storePath;
    state = &SubstitutionGoal::init;
}


void SubstitutionGoal::work()
{
    (this->*state)();
}


void SubstitutionGoal::init()
{
    trace("init");

    /* If the path already exists we're done. */
    if (isValidPath(storePath)) {
        amDone();
        return;
    }

    /* Otherwise, get the substitutes. */
    subs = querySubstitutes(storePath);

    /* Try the first one. */
    tryNext();
}


void SubstitutionGoal::tryNext()
{
    trace("trying next substitute");

    if (subs.size() == 0) throw Error(
        format("path `%1%' is required, but it has no (remaining) substitutes")
            % storePath);
    sub = subs.front();
    subs.pop_front();

    /* Normalise the substitute store expression. */
    worker.addNormalisationGoal(sub.storeExpr, shared_from_this());
    nrWaitees = 1;

    state = &SubstitutionGoal::exprNormalised;
}


void SubstitutionGoal::exprNormalised()
{
    trace("substitute store expression normalised");

    /* Realise the substitute store expression. */
    if (!querySuccessor(sub.storeExpr, nfSub))
        nfSub = sub.storeExpr;
    worker.addRealisationGoal(nfSub, shared_from_this());
    nrWaitees = 1;

    state = &SubstitutionGoal::exprRealised;
}


void SubstitutionGoal::exprRealised()
{
    trace("substitute store expression realised");

    state = &SubstitutionGoal::tryToRun;
    worker.waitForBuildSlot(shared_from_this());
}


void SubstitutionGoal::tryToRun()
{
    trace("trying to run");

    /* Make sure that we are allowed to start a build. */
    if (!worker.canBuildMore()) {
        worker.waitForBuildSlot(shared_from_this());
        return;
    }

    /* Acquire a lock on the output path. */
    PathSet lockPath;
    lockPath.insert(storePath);
    outputLock.lockPaths(lockPath);

    /* Check again whether the path is invalid. */
    if (isValidPath(storePath)) {
        debug(format("store path `%1%' has become valid") % storePath);
        outputLock.setDeletion(true);
        amDone();
        return;
    }

    startNest(nest, lvlInfo,
        format("substituting path `%1%'") % storePath);
    
    /* What's the substitute program? */
    StoreExpr expr = storeExprFromPath(nfSub);
    assert(expr.type == StoreExpr::neClosure);
    assert(!expr.closure.roots.empty());
    Path program =
        canonPath(*expr.closure.roots.begin() + "/" + sub.program);

    logPipe.create();

    /* Remove the (stale) output path if it exists. */
    if (pathExists(storePath))
        deletePath(storePath);

    /* Fork the substitute program. */
    pid = fork();
    switch (pid) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            logPipe.readSide.close();

            /* !!! close other handles */

            commonChildInit(logPipe);

            /* Fill in the arguments. */
            Strings args(sub.args);
            args.push_front(storePath);
            args.push_front(baseNameOf(program));
            const char * * argArr = strings2CharPtrs(args);

            execv(program.c_str(), (char * *) argArr);
            
            throw SysError(format("executing `%1%'") % program);
            
        } catch (exception & e) {
            cerr << format("substitute error: %1%\n") % e.what();
        }
        _exit(1);
    }
    
    /* parent */
    pid.setSeparatePG(true);
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, logPipe.readSide, true);

    state = &SubstitutionGoal::finished;
}


void SubstitutionGoal::finished()
{
    trace("substitute finished");

    /* Since we got an EOF on the logger pipe, the substitute is
       presumed to have terminated.  */
    /* !!! this could block! */
    pid_t savedPid = pid;
    int status = pid.wait(true);

    /* So the child is gone now. */
    worker.childTerminated(savedPid);

    /* Close the read side of the logger pipe. */
    logPipe.readSide.close();

    debug(format("substitute for `%1%' finished") % storePath);

    /* Check the exit status. */
    if (!statusOk(status))
        throw Error(format("builder for `%1%' %2%")
            % storePath % statusToString(status));

    if (!pathExists(storePath))
        throw Error(format("substitute did not produce path `%1%'") % storePath);

    Transaction txn;
    createStoreTransaction(txn);
    registerValidPath(txn, storePath);
    txn.commit();

    outputLock.setDeletion(true);
    
    amDone();
}


void SubstitutionGoal::trace(const format & f)
{
    debug(format("substitution of `%1%': %2%") % storePath % f);
}



//////////////////////////////////////////////////////////////////////


static bool working = false;


Worker::Worker()
{
    /* Debugging: prevent recursive workers. */ 
    if (working) abort();
    working = true;
    nrChildren = 0;
}


Worker::~Worker()
{
    working = false;
}


template<class T>
static void addGoal(const Path & path, GoalPtr waiter,
    Worker & worker, Goals & goals,
    GoalMap & goalMap, GoalMapRev & goalMapRev)
{
    GoalPtr goal;
    goal = goalMap[path];
    if (!goal) {
        goal = GoalPtr(new T(path, worker));
        goals.insert(goal);
        goalMap[path] = goal;
        goalMapRev[goal] = path;
        worker.wakeUp(goal);
    }
    if (waiter) goal->addWaiter(waiter);
}


void Worker::addNormalisationGoal(const Path & nePath, GoalPtr waiter)
{
    addGoal<NormalisationGoal>(nePath, waiter, *this, goals,
        normalisationGoals, normalisationGoalsRev);
}


void Worker::addRealisationGoal(const Path & nePath, GoalPtr waiter)
{
    addGoal<RealisationGoal>(nePath, waiter, *this, goals,
        realisationGoals, realisationGoalsRev);
}


void Worker::addSubstitutionGoal(const Path & storePath, GoalPtr waiter)
{
    addGoal<SubstitutionGoal>(storePath, waiter, *this, goals,
        substitutionGoals, substitutionGoalsRev);
}


static void removeGoal(GoalPtr goal,
    GoalMap & goalMap, GoalMapRev & goalMapRev)
{
    GoalMapRev::iterator i = goalMapRev.find(goal);
    if (i != goalMapRev.end()) {
        goalMapRev.erase(i);
        goalMap.erase(i->second);
    }
}


void Worker::removeGoal(GoalPtr goal)
{
    goals.erase(goal);
    ::removeGoal(goal, normalisationGoals, normalisationGoalsRev);
    ::removeGoal(goal, realisationGoals, realisationGoalsRev);
    ::removeGoal(goal, substitutionGoals, substitutionGoalsRev);
}


void Worker::wakeUp(GoalPtr goal)
{
    printMsg(lvlVomit, "wake up");
    awake.insert(goal);
}


bool Worker::canBuildMore()
{
    return nrChildren < maxBuildJobs;
}


void Worker::childStarted(GoalPtr goal,
    pid_t pid, int fdOutput, bool inBuildSlot)
{
    Child child;
    child.goal = goal;
    child.fdOutput = fdOutput;
    child.inBuildSlot = inBuildSlot;
    children[pid] = child;
    if (inBuildSlot) nrChildren++;
}


void Worker::childTerminated(pid_t pid)
{
    Children::iterator i = children.find(pid);
    assert(i != children.end());

    if (i->second.inBuildSlot) {
        assert(nrChildren > 0);
        nrChildren--;
    }

    children.erase(pid);

    /* Wake up goals waiting for a build slot. */
    for (Goals::iterator i = wantingToBuild.begin();
         i != wantingToBuild.end(); ++i)
        wakeUp(*i);
    wantingToBuild.clear();
}


void Worker::waitForBuildSlot(GoalPtr goal)
{
    debug("wait for build slot");
    if (canBuildMore())
        wakeUp(goal); /* we can do it right away */
    else
        wantingToBuild.insert(goal);
}


void Worker::run()
{
    startNest(nest, lvlDebug, format("entered goal loop"));

    while (1) {

        checkInterrupt();

        /* Call every wake goal. */
        while (!awake.empty()) {
            Goals awake2(awake); /* !!! why is this necessary? */
            awake.clear();
            for (Goals::iterator i = awake2.begin(); i != awake2.end(); ++i) {
                printMsg(lvlVomit,
                    format("running goal (%1% left)") % goals.size());
                checkInterrupt();
                GoalPtr goal = *i;
                goal->work();
            }
        }

        if (goals.empty()) break;

        /* !!! not when we're polling */
        assert(!children.empty());
        
        /* Wait for input. */
        waitForInput();
    }

    assert(awake.empty());
    assert(wantingToBuild.empty());
    assert(children.empty());
}


void Worker::waitForInput()
{
    printMsg(lvlVomit, "waiting for children");

    /* Process log output from the children.  We also use this to
       detect child termination: if we get EOF on the logger pipe of a
       build, we assume that the builder has terminated. */

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    for (Children::iterator i = children.begin();
         i != children.end(); ++i)
    {
        int fd = i->second.fdOutput;
        FD_SET(fd, &fds);
        if (fd >= fdMax) fdMax = fd + 1;
    }

    if (select(fdMax, &fds, 0, 0, 0) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    /* Process all available file descriptors. */
    for (Children::iterator i = children.begin();
         i != children.end(); ++i)
    {
        checkInterrupt();
        GoalPtr goal = i->second.goal;
        int fd = i->second.fdOutput;
        if (FD_ISSET(fd, &fds)) {
            unsigned char buffer[4096];
            ssize_t rd = read(fd, buffer, sizeof(buffer));
            if (rd == -1) {
                if (errno != EINTR)
                    throw SysError(format("reading from `%1%'")
                        % goal->name());
            } else if (rd == 0) {
                debug(format("EOF on `%1%'") % goal->name());
                wakeUp(goal);
            } else {
                printMsg(lvlVomit, format("read %1% bytes from `%2%'")
                    % rd % goal->name());
//                 writeFull(goal.fdLogFile, buffer, rd);
                if (verbosity >= buildVerbosity)
                    writeFull(STDERR_FILENO, buffer, rd);
            }
        }
    }
}


//////////////////////////////////////////////////////////////////////


Path normaliseStoreExpr(const Path & nePath)
{
    startNest(nest, lvlDebug, format("normalising `%1%'") % nePath);

    Worker worker;
    worker.addNormalisationGoal(nePath, GoalPtr());
    worker.run();

    Path nfPath;
    if (!querySuccessor(nePath, nfPath))
        throw Error("there should be a successor");
    
    return nfPath;
}


void realiseClosure(const Path & nePath)
{
    startNest(nest, lvlDebug, format("realising closure `%1%'") % nePath);

    Worker worker;
    worker.addRealisationGoal(nePath, GoalPtr());
    worker.run();
}


void ensurePath(const Path & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker;
    worker.addSubstitutionGoal(path, GoalPtr());
    worker.run();
}
