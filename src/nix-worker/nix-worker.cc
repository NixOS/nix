#include "shared.hh"
#include "local-store.hh"
#include "util.hh"
#include "serialise.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "globals.hh"

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

using namespace nix;


#ifndef SIGPOLL
#define SIGPOLL SIGIO
#endif


static FdSource from(STDIN_FILENO);
static FdSink to(STDOUT_FILENO);

bool canSendStderr;
pid_t myPid;



/* This function is called anytime we want to write something to
   stderr.  If we're in a state where the protocol allows it (i.e.,
   when canSendStderr), send the message to the client over the
   socket. */
static void tunnelStderr(const unsigned char * buf, size_t count)
{
    /* Don't send the message to the client if we're a child of the
       process handling the connection.  Otherwise we could screw up
       the protocol.  It's up to the parent to redirect stderr and
       send it to the client somehow (e.g., as in build.cc). */
    if (canSendStderr && myPid == getpid()) {
        try {
            writeInt(STDERR_NEXT, to);
            writeString(string((char *) buf, count), to);
        } catch (...) {
            /* Write failed; that means that the other side is
               gone. */
            canSendStderr = false;
            throw;
        }
    } else
        writeFull(STDERR_FILENO, buf, count);
}


/* Return true if the remote side has closed its end of the
   connection, false otherwise.  Should not be called on any socket on
   which we expect input! */
static bool isFarSideClosed(int socket)
{
    struct timeval timeout;
    timeout.tv_sec = timeout.tv_usec = 0;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket, &fds);

    while (select(socket + 1, &fds, 0, 0, &timeout) == -1)
        if (errno != EINTR) throw SysError("select()");

    if (!FD_ISSET(socket, &fds)) return false;

    /* Destructive read to determine whether the select() marked the
       socket as readable because there is actual input or because
       we've reached EOF (i.e., a read of size 0 is available). */
    char c;
    int rd;
    if ((rd = read(socket, &c, 1)) > 0)
        throw Error("EOF expected (protocol error?)");
    else if (rd == -1 && errno != ECONNRESET)
        throw SysError("expected connection reset or EOF");
    
    return true;
}


/* A SIGPOLL signal is received when data is available on the client
   communication scoket, or when the client has closed its side of the
   socket.  This handler is enabled at precisely those moments in the
   protocol when we're doing work and the client is supposed to be
   quiet.  Thus, if we get a SIGPOLL signal, it means that the client
   has quit.  So we should quit as well.

   Too bad most operating systems don't support the POLL_HUP value for
   si_code in siginfo_t.  That would make most of the SIGPOLL
   complexity unnecessary, i.e., we could just enable SIGPOLL all the
   time and wouldn't have to worry about races. */
static void sigPollHandler(int sigNo)
{
    try {
        /* Check that the far side actually closed.  We're still
           getting spurious signals every once in a while.  I.e.,
           there is no input available, but we get a signal with
           POLL_IN set.  Maybe it's delayed or something. */
        if (isFarSideClosed(from.fd)) {
            if (!blockInt) {
                _isInterrupted = 1;
                blockInt = 1;
                canSendStderr = false;
                string s = "SIGPOLL\n";
                write(STDERR_FILENO, s.c_str(), s.size());
            }
        } else {
            string s = "spurious SIGPOLL\n";
            write(STDERR_FILENO, s.c_str(), s.size());
        }
    }
    catch (Error & e) {
        /* Shouldn't happen. */
        string s = "impossible: " + e.msg() + '\n';
        write(STDERR_FILENO, s.c_str(), s.size());
        throw;
    }
}


static void setSigPollAction(bool enable)
{
    struct sigaction act, oact;
    act.sa_handler = enable ? sigPollHandler : SIG_IGN;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGPOLL, &act, &oact))
        throw SysError("setting handler for SIGPOLL");
}


/* startWork() means that we're starting an operation for which we
   want to send out stderr to the client. */
static void startWork()
{
    canSendStderr = true;

    /* Handle client death asynchronously. */
    setSigPollAction(true);

    /* Of course, there is a race condition here: the socket could
       have closed between when we last read from / wrote to it, and
       between the time we set the handler for SIGPOLL.  In that case
       we won't get the signal.  So do a non-blocking select() to find
       out if any input is available on the socket.  If there is, it
       has to be the 0-byte read that indicates that the socket has
       closed. */
    if (isFarSideClosed(from.fd)) {
        _isInterrupted = 1;
        checkInterrupt();
    }
}


/* stopWork() means that we're done; stop sending stderr to the
   client. */
static void stopWork(bool success = true, const string & msg = "")
{
    /* Stop handling async client death; we're going to a state where
       we're either sending or receiving from the client, so we'll be
       notified of client death anyway. */
    setSigPollAction(false);
    
    canSendStderr = false;

    if (success)
        writeInt(STDERR_LAST, to);
    else {
        writeInt(STDERR_ERROR, to);
        writeString(msg, to);
    }
}


struct TunnelSink : Sink
{
    Sink & to;
    TunnelSink(Sink & to) : to(to)
    {
    }
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        writeInt(STDERR_WRITE, to);
        writeString(string((const char *) data, len), to);
    }
};


struct TunnelSource : Source
{
    Source & from;
    TunnelSource(Source & from) : from(from)
    {
    }
    virtual void operator ()
        (unsigned char * data, unsigned int len)
    {
        /* Careful: we're going to receive data from the client now,
           so we have to disable the SIGPOLL handler. */
        setSigPollAction(false);
        canSendStderr = false;
        
        writeInt(STDERR_READ, to);
        writeInt(len, to);
        string s = readString(from);
        if (s.size() != len) throw Error("not enough data");
        memcpy(data, (const unsigned char *) s.c_str(), len);

        startWork();
    }
};


static void performOp(unsigned int clientVersion,
    Source & from, Sink & to, unsigned int op)
{
    switch (op) {

#if 0        
    case wopQuit: {
        /* Close the database. */
        store.reset((StoreAPI *) 0);
        writeInt(1, to);
        break;
    }
#endif

    case wopIsValidPath: {
        Path path = readStorePath(from);
        startWork();
        bool result = store->isValidPath(path);
        stopWork();
        writeInt(result, to);
        break;
    }

    case wopHasSubstitutes: {
        Path path = readStorePath(from);
        startWork();
        bool result = store->hasSubstitutes(path);
        stopWork();
        writeInt(result, to);
        break;
    }

    case wopQueryPathHash: {
        Path path = readStorePath(from);
        startWork();
        Hash hash = store->queryPathHash(path);
        stopWork();
        writeString(printHash(hash), to);
        break;
    }

    case wopQueryReferences:
    case wopQueryReferrers: {
        Path path = readStorePath(from);
        startWork();
        PathSet paths;
        if (op == wopQueryReferences)
            store->queryReferences(path, paths);
        else
            store->queryReferrers(path, paths);
        stopWork();
        writeStringSet(paths, to);
        break;
    }

    case wopQueryDeriver: {
        Path path = readStorePath(from);
        startWork();
        Path deriver = store->queryDeriver(path);
        stopWork();
        writeString(deriver, to);
        break;
    }

    case wopAddToStore: {
        /* !!! uberquick hack */
        string baseName = readString(from);
        bool fixed = readInt(from) == 1;
        bool recursive = readInt(from) == 1;
        string hashAlgo = readString(from);
        
        Path tmp = createTempDir();
        AutoDelete delTmp(tmp);
        Path tmp2 = tmp + "/" + baseName;
        restorePath(tmp2, from);

        startWork();
        Path path = store->addToStore(tmp2, fixed, recursive, hashAlgo);
        stopWork();
        
        writeString(path, to);
        break;
    }

    case wopAddTextToStore: {
        string suffix = readString(from);
        string s = readString(from);
        PathSet refs = readStorePaths(from);
        startWork();
        Path path = store->addTextToStore(suffix, s, refs);
        stopWork();
        writeString(path, to);
        break;
    }

    case wopExportPath: {
        Path path = readStorePath(from);
        bool sign = readInt(from) == 1;
        startWork();
        TunnelSink sink(to);
        store->exportPath(path, sign, sink);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopImportPath: {
        startWork();
        TunnelSource source(from);
        Path path = store->importPath(true, source);
        stopWork();
        writeString(path, to);
        break;
    }

    case wopBuildDerivations: {
        PathSet drvs = readStorePaths(from);
        startWork();
        store->buildDerivations(drvs);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopEnsurePath: {
        Path path = readStorePath(from);
        startWork();
        store->ensurePath(path);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopAddTempRoot: {
        Path path = readStorePath(from);
        startWork();
        store->addTempRoot(path);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopAddIndirectRoot: {
        Path path = absPath(readString(from));
        startWork();
        store->addIndirectRoot(path);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopSyncWithGC: {
        startWork();
        store->syncWithGC();
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopFindRoots: {
        startWork();
        Roots roots = store->findRoots();
        stopWork();
        writeInt(roots.size(), to);
        for (Roots::iterator i = roots.begin(); i != roots.end(); ++i) {
            writeString(i->first, to);
            writeString(i->second, to);
        }
        break;
    }

    case wopCollectGarbage: {
        GCOptions options;
        options.action = (GCOptions::GCAction) readInt(from);
        options.pathsToDelete = readStorePaths(from);
        options.ignoreLiveness = readInt(from);
        options.maxFreed = readLongLong(from);
        options.maxLinks = readInt(from);

        GCResults results;
        
        startWork();
        if (options.ignoreLiveness)
            throw Error("you are not allowed to ignore liveness");
        store->collectGarbage(options, results);
        stopWork();
        
        writeStringSet(results.paths, to);
        writeLongLong(results.bytesFreed, to);
        writeLongLong(results.blocksFreed, to);
        
        break;
    }

    case wopSetOptions: {
        keepFailed = readInt(from) != 0;
        keepGoing = readInt(from) != 0;
        tryFallback = readInt(from) != 0;
        verbosity = (Verbosity) readInt(from);
        maxBuildJobs = readInt(from);
        maxSilentTime = readInt(from);
        if (GET_PROTOCOL_MINOR(clientVersion) >= 2)
            useBuildHook = readInt(from) != 0;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 4) {
            buildVerbosity = (Verbosity) readInt(from);
            logType = (LogType) readInt(from);
            printBuildTrace = readInt(from) != 0;
        }
        startWork();
        stopWork();
        break;
    }

    case wopQuerySubstitutablePathInfo: {
        Path path = absPath(readString(from));
        startWork();
        SubstitutablePathInfo info;
        bool res = store->querySubstitutablePathInfo(path, info);
        stopWork();
        writeInt(res ? 1 : 0, to);
        if (res) {
            writeString(info.deriver, to);
            writeStringSet(info.references, to);
            writeLongLong(info.downloadSize, to);
        }
        break;
    }
            
    default:
        throw Error(format("invalid operation %1%") % op);
    }
}


static void processConnection()
{
    RemoveTempRoots removeTempRoots __attribute__((unused));

    canSendStderr = false;
    myPid = getpid();    
    writeToStderr = tunnelStderr;

    /* Allow us to receive SIGPOLL for events on the client socket. */
    setSigPollAction(false);
    if (fcntl(from.fd, F_SETOWN, getpid()) == -1)
        throw SysError("F_SETOWN");
    if (fcntl(from.fd, F_SETFL, fcntl(from.fd, F_GETFL, 0) | FASYNC) == -1)
        throw SysError("F_SETFL");

    /* Exchange the greeting. */
    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");
    writeInt(WORKER_MAGIC_2, to);

    writeInt(PROTOCOL_VERSION, to);
    unsigned int clientVersion = readInt(from);

    /* Send startup error messages to the client. */
    startWork();

    try {

        /* If we can't accept clientVersion, then throw an error
           *here* (not above). */

#if 0
        /* Prevent users from doing something very dangerous. */
        if (geteuid() == 0 &&
            querySetting("build-users-group", "") == "")
            throw Error("if you run `nix-worker' as root, then you MUST set `build-users-group'!");
#endif

        /* Open the store. */
        store = boost::shared_ptr<StoreAPI>(new LocalStore());

        stopWork();
        
    } catch (Error & e) {
        stopWork(false, e.msg());
        return;
    }

    /* Process client requests. */
    unsigned int opCount = 0;
    
    while (true) {
        WorkerOp op;
        try {
            op = (WorkerOp) readInt(from);
        } catch (EndOfFile & e) {
            break;
        }

        opCount++;

        try {
            performOp(clientVersion, from, to, op);
        } catch (Error & e) {
            stopWork(false, e.msg());
        }

        assert(!canSendStderr);
    };

    printMsg(lvlError, format("%1% worker operations") % opCount);
}


static void sigChldHandler(int sigNo)
{
    /* Reap all dead children. */
    while (waitpid(-1, 0, WNOHANG) > 0) ;
}


static void setSigChldAction(bool autoReap)
{
    struct sigaction act, oact;
    act.sa_handler = autoReap ? sigChldHandler : SIG_DFL;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, &oact))
        throw SysError("setting SIGCHLD handler");
}


static void daemonLoop()
{
    /* Get rid of children automatically; don't let them become
       zombies. */
    setSigChldAction(true);
    
    /* Create and bind to a Unix domain socket. */
    AutoCloseFD fdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
        throw SysError("cannot create Unix domain socket");

    string socketPath = nixStateDir + DEFAULT_SOCKET_PATH;

    createDirs(dirOf(socketPath));

    /* Urgh, sockaddr_un allows path names of only 108 characters.  So
       chdir to the socket directory so that we can pass a relative
       path name. */
    chdir(dirOf(socketPath).c_str());
    Path socketPathRel = "./" + baseNameOf(socketPath);
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPathRel.size() >= sizeof(addr.sun_path))
        throw Error(format("socket path `%1%' is too long") % socketPathRel);
    strcpy(addr.sun_path, socketPathRel.c_str());

    unlink(socketPath.c_str());

    /* Make sure that the socket is created with 0666 permission
       (everybody can connect --- provided they have access to the
       directory containing the socket). */
    mode_t oldMode = umask(0111);
    int res = bind(fdSocket, (struct sockaddr *) &addr, sizeof(addr));
    umask(oldMode);
    if (res == -1)
        throw SysError(format("cannot bind to socket `%1%'") % socketPath);

    chdir("/"); /* back to the root */

    if (listen(fdSocket, 5) == -1)
        throw SysError(format("cannot listen on socket `%1%'") % socketPath);

    /* Loop accepting connections. */
    while (1) {

        try {
            /* Important: the server process *cannot* open the
               Berkeley DB environment, because it doesn't like forks
               very much. */
            assert(!store);
            
            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(fdSocket,
                (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            checkInterrupt();
            if (remote == -1)
		if (errno == EINTR)
		    continue;
		else
		    throw SysError("accepting connection");

            printMsg(lvlInfo, format("accepted connection %1%") % remote);

            /* Fork a child to handle the connection. */
            pid_t child;
            child = fork();
    
            switch (child) {
        
            case -1:
                throw SysError("unable to fork");

            case 0:
                try { /* child */

                    /* Background the worker. */
                    if (setsid() == -1)
                        throw SysError(format("creating a new session"));

                    /* Restore normal handling of SIGCHLD. */
                    setSigChldAction(false);

                    /* Since the daemon can be long-running, the
                       settings may have changed.  So force a reload. */
                    reloadSettings();
                    
                    /* Handle the connection. */
                    from.fd = remote;
                    to.fd = remote;
                    processConnection();
                    
                } catch (std::exception & e) {
                    std::cerr << format("child error: %1%\n") % e.what();
                }
                exit(0);
            }

        } catch (Interrupted & e) {
            throw;
        } catch (Error & e) {
            printMsg(lvlError, format("error processing connection: %1%") % e.msg());
        }
    }
}


void run(Strings args)
{
    bool slave = false;
    bool daemon = false;
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--slave") slave = true;
        if (arg == "--daemon") daemon = true;
    }

    if (slave) {
        /* This prevents us from receiving signals from the terminal
           when we're running in setuid mode. */
        if (setsid() == -1)
            throw SysError(format("creating a new session"));

        processConnection();
    }

    else if (daemon) {
        if (setuidMode)
            throw Error("daemon cannot be started in setuid mode");
        chdir("/");
        daemonLoop();
    }

    else
        throw Error("must be run in either --slave or --daemon mode");
}


#include "help.txt.hh"

void printHelp()
{
    std::cout << string((char *) helpText, sizeof helpText);
}


string programId = "nix-worker";
