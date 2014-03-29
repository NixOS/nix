#include "shared.hh"
#include "local-store.hh"
#include "util.hh"
#include "serialise.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"

#include <cstring>
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


/* On platforms that have O_ASYNC, we can detect when a client
   disconnects and immediately kill any ongoing builds.  On platforms
   that lack it, we only notice the disconnection the next time we try
   to write to the client.  So if you have a builder that never
   generates output on stdout/stderr, the daemon will never notice
   that the client has disconnected until the builder terminates.

   GNU/Hurd does have O_ASYNC, but its Unix-domain socket translator
   (pflocal) does not implement F_SETOWN.  See
   <http://lists.gnu.org/archive/html/bug-guix/2013-07/msg00021.html> for
   details.*/
#if defined O_ASYNC && !defined __GNU__
#define HAVE_HUP_NOTIFICATION
#ifndef SIGPOLL
#define SIGPOLL SIGIO
#endif
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
            writeString(buf, count, to);
            to.flush();
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
   communication socket, or when the client has closed its side of the
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
    using namespace std;
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
                const char * s = "SIGPOLL\n";
                write(STDERR_FILENO, s, strlen(s));
            }
        } else {
            const char * s = "spurious SIGPOLL\n";
            write(STDERR_FILENO, s, strlen(s));
        }
    }
    catch (Error & e) {
        /* Shouldn't happen. */
        string s = "impossible: " + e.msg() + '\n';
        write(STDERR_FILENO, s.data(), s.size());
        throw;
    }
}


static void setSigPollAction(bool enable)
{
#ifdef HAVE_HUP_NOTIFICATION
    struct sigaction act, oact;
    act.sa_handler = enable ? sigPollHandler : SIG_IGN;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGPOLL, &act, &oact))
        throw SysError("setting handler for SIGPOLL");
#endif
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
static void stopWork(bool success = true, const string & msg = "", unsigned int status = 0)
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
        if (status != 0) writeInt(status, to);
    }
}


struct TunnelSink : Sink
{
    Sink & to;
    TunnelSink(Sink & to) : to(to) { }
    virtual void operator () (const unsigned char * data, size_t len)
    {
        writeInt(STDERR_WRITE, to);
        writeString(data, len, to);
    }
};


struct TunnelSource : BufferedSource
{
    Source & from;
    TunnelSource(Source & from) : from(from) { }
    size_t readUnbuffered(unsigned char * data, size_t len)
    {
        /* Careful: we're going to receive data from the client now,
           so we have to disable the SIGPOLL handler. */
        setSigPollAction(false);
        canSendStderr = false;

        writeInt(STDERR_READ, to);
        writeInt(len, to);
        to.flush();
        size_t n = readString(data, len, from);

        startWork();
        if (n == 0) throw EndOfFile("unexpected end-of-file");
        return n;
    }
};


/* If the NAR archive contains a single file at top-level, then save
   the contents of the file to `s'.  Otherwise barf. */
struct RetrieveRegularNARSink : ParseSink
{
    bool regular;
    string s;

    RetrieveRegularNARSink() : regular(true) { }

    void createDirectory(const Path & path)
    {
        regular = false;
    }

    void receiveContents(unsigned char * data, unsigned int len)
    {
        s.append((const char *) data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        regular = false;
    }
};


/* Adapter class of a Source that saves all data read to `s'. */
struct SavingSourceAdapter : Source
{
    Source & orig;
    string s;
    SavingSourceAdapter(Source & orig) : orig(orig) { }
    size_t read(unsigned char * data, size_t len)
    {
        size_t n = orig.read(data, len);
        s.append((const char *) data, n);
        return n;
    }
};


static void performOp(bool trusted, unsigned int clientVersion,
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
        /* 'readStorePath' could raise an error leading to the connection
           being closed.  To be able to recover from an invalid path error,
           call 'startWork' early, and do 'assertStorePath' afterwards so
           that the 'Error' exception handler doesn't close the
           connection.  */
        Path path = readString(from);
        startWork();
        assertStorePath(path);
        bool result = store->isValidPath(path);
        stopWork();
        writeInt(result, to);
        break;
    }

    case wopQueryValidPaths: {
        PathSet paths = readStorePaths<PathSet>(from);
        startWork();
        PathSet res = store->queryValidPaths(paths);
        stopWork();
        writeStrings(res, to);
        break;
    }

    case wopHasSubstitutes: {
        Path path = readStorePath(from);
        startWork();
        PathSet res = store->querySubstitutablePaths(singleton<PathSet>(path));
        stopWork();
        writeInt(res.find(path) != res.end(), to);
        break;
    }

    case wopQuerySubstitutablePaths: {
        PathSet paths = readStorePaths<PathSet>(from);
        startWork();
        PathSet res = store->querySubstitutablePaths(paths);
        stopWork();
        writeStrings(res, to);
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
    case wopQueryReferrers:
    case wopQueryValidDerivers:
    case wopQueryDerivationOutputs: {
        Path path = readStorePath(from);
        startWork();
        PathSet paths;
        if (op == wopQueryReferences)
            store->queryReferences(path, paths);
        else if (op == wopQueryReferrers)
            store->queryReferrers(path, paths);
        else if (op == wopQueryValidDerivers)
            paths = store->queryValidDerivers(path);
        else paths = store->queryDerivationOutputs(path);
        stopWork();
        writeStrings(paths, to);
        break;
    }

    case wopQueryDerivationOutputNames: {
        Path path = readStorePath(from);
        startWork();
        StringSet names;
        names = store->queryDerivationOutputNames(path);
        stopWork();
        writeStrings(names, to);
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

    case wopQueryPathFromHashPart: {
        string hashPart = readString(from);
        startWork();
        Path path = store->queryPathFromHashPart(hashPart);
        stopWork();
        writeString(path, to);
        break;
    }

    case wopAddToStore: {
        string baseName = readString(from);
        bool fixed = readInt(from) == 1; /* obsolete */
        bool recursive = readInt(from) == 1;
        string s = readString(from);
        /* Compatibility hack. */
        if (!fixed) {
            s = "sha256";
            recursive = true;
        }
        HashType hashAlgo = parseHashType(s);

        SavingSourceAdapter savedNAR(from);
        RetrieveRegularNARSink savedRegular;

        if (recursive) {
            /* Get the entire NAR dump from the client and save it to
               a string so that we can pass it to
               addToStoreFromDump(). */
            ParseSink sink; /* null sink; just parse the NAR */
            parseDump(sink, savedNAR);
        } else
            parseDump(savedRegular, from);

        startWork();
        if (!savedRegular.regular) throw Error("regular file expected");
        Path path = dynamic_cast<LocalStore *>(store.get())
            ->addToStoreFromDump(recursive ? savedNAR.s : savedRegular.s, baseName, recursive, hashAlgo);
        stopWork();

        writeString(path, to);
        break;
    }

    case wopAddTextToStore: {
        string suffix = readString(from);
        string s = readString(from);
        PathSet refs = readStorePaths<PathSet>(from);
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

    case wopImportPaths: {
        startWork();
        TunnelSource source(from);
        Paths paths = store->importPaths(true, source);
        stopWork();
        writeStrings(paths, to);
        break;
    }

    case wopBuildPaths: {
        PathSet drvs = readStorePaths<PathSet>(from);
        startWork();
        store->buildPaths(drvs);
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
        options.pathsToDelete = readStorePaths<PathSet>(from);
        options.ignoreLiveness = readInt(from);
        options.maxFreed = readLongLong(from);
        readInt(from); // obsolete field
        if (GET_PROTOCOL_MINOR(clientVersion) >= 5) {
            /* removed options */
            readInt(from);
            readInt(from);
        }

        GCResults results;

        startWork();
        if (options.ignoreLiveness)
            throw Error("you are not allowed to ignore liveness");
        store->collectGarbage(options, results);
        stopWork();

        writeStrings(results.paths, to);
        writeLongLong(results.bytesFreed, to);
        writeLongLong(0, to); // obsolete

        break;
    }

    case wopSetOptions: {
        settings.keepFailed = readInt(from) != 0;
        settings.keepGoing = readInt(from) != 0;
        settings.set("build-fallback", readInt(from) ? "true" : "false");
        verbosity = (Verbosity) readInt(from);
        settings.set("build-max-jobs", int2String(readInt(from)));
        settings.set("build-max-silent-time", int2String(readInt(from)));
        if (GET_PROTOCOL_MINOR(clientVersion) >= 2)
            settings.useBuildHook = readInt(from) != 0;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 4) {
            settings.buildVerbosity = (Verbosity) readInt(from);
            logType = (LogType) readInt(from);
            settings.printBuildTrace = readInt(from) != 0;
        }
        if (GET_PROTOCOL_MINOR(clientVersion) >= 6)
            settings.set("build-cores", int2String(readInt(from)));
        if (GET_PROTOCOL_MINOR(clientVersion) >= 10)
            settings.set("build-use-substitutes", readInt(from) ? "true" : "false");
        if (GET_PROTOCOL_MINOR(clientVersion) >= 12) {
            unsigned int n = readInt(from);
            for (unsigned int i = 0; i < n; i++) {
                string name = readString(from);
                string value = readString(from);
                if (name == "build-timeout" || name == "use-ssh-substituter")
                    settings.set(name, value);
                else
                    settings.set(trusted ? name : "untrusted-" + name, value);
            }
        }
        settings.update();
        startWork();
        stopWork();
        break;
    }

    case wopQuerySubstitutablePathInfo: {
        Path path = absPath(readString(from));
        startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos(singleton<PathSet>(path), infos);
        stopWork();
        SubstitutablePathInfos::iterator i = infos.find(path);
        if (i == infos.end())
            writeInt(0, to);
        else {
            writeInt(1, to);
            writeString(i->second.deriver, to);
            writeStrings(i->second.references, to);
            writeLongLong(i->second.downloadSize, to);
            if (GET_PROTOCOL_MINOR(clientVersion) >= 7)
                writeLongLong(i->second.narSize, to);
        }
        break;
    }

    case wopQuerySubstitutablePathInfos: {
        PathSet paths = readStorePaths<PathSet>(from);
        startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos(paths, infos);
        stopWork();
        writeInt(infos.size(), to);
        foreach (SubstitutablePathInfos::iterator, i, infos) {
            writeString(i->first, to);
            writeString(i->second.deriver, to);
            writeStrings(i->second.references, to);
            writeLongLong(i->second.downloadSize, to);
            writeLongLong(i->second.narSize, to);
        }
        break;
    }

    case wopQueryAllValidPaths: {
        startWork();
        PathSet paths = store->queryAllValidPaths();
        stopWork();
        writeStrings(paths, to);
        break;
    }

    case wopQueryFailedPaths: {
        startWork();
        PathSet paths = store->queryFailedPaths();
        stopWork();
        writeStrings(paths, to);
        break;
    }

    case wopClearFailedPaths: {
        PathSet paths = readStrings<PathSet>(from);
        startWork();
        store->clearFailedPaths(paths);
        stopWork();
        writeInt(1, to);
        break;
    }

    case wopQueryPathInfo: {
        Path path = readStorePath(from);
        startWork();
        ValidPathInfo info = store->queryPathInfo(path);
        stopWork();
        writeString(info.deriver, to);
        writeString(printHash(info.hash), to);
        writeStrings(info.references, to);
        writeInt(info.registrationTime, to);
        writeLongLong(info.narSize, to);
        break;
    }

    default:
        throw Error(format("invalid operation %1%") % op);
    }
}


static void processConnection(bool trusted)
{
    canSendStderr = false;
    myPid = getpid();
    _writeToStderr = tunnelStderr;

#ifdef HAVE_HUP_NOTIFICATION
    /* Allow us to receive SIGPOLL for events on the client socket. */
    setSigPollAction(false);
    if (fcntl(from.fd, F_SETOWN, getpid()) == -1)
        throw SysError("F_SETOWN");
    if (fcntl(from.fd, F_SETFL, fcntl(from.fd, F_GETFL, 0) | O_ASYNC) == -1)
        throw SysError("F_SETFL");
#endif

    /* Exchange the greeting. */
    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");
    writeInt(WORKER_MAGIC_2, to);
    writeInt(PROTOCOL_VERSION, to);
    to.flush();
    unsigned int clientVersion = readInt(from);

    if (GET_PROTOCOL_MINOR(clientVersion) >= 14 && readInt(from))
        setAffinityTo(readInt(from));

    bool reserveSpace = true;
    if (GET_PROTOCOL_MINOR(clientVersion) >= 11)
        reserveSpace = readInt(from) != 0;

    /* Send startup error messages to the client. */
    startWork();

    try {

        /* If we can't accept clientVersion, then throw an error
           *here* (not above). */

#if 0
        /* Prevent users from doing something very dangerous. */
        if (geteuid() == 0 &&
            querySetting("build-users-group", "") == "")
            throw Error("if you run `nix-daemon' as root, then you MUST set `build-users-group'!");
#endif

        /* Open the store. */
        store = std::shared_ptr<StoreAPI>(new LocalStore(reserveSpace));

        stopWork();
        to.flush();

    } catch (Error & e) {
        stopWork(false, e.msg());
        to.flush();
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
            performOp(trusted, clientVersion, from, to, op);
        } catch (Error & e) {
            /* If we're not in a state where we can send replies, then
               something went wrong processing the input of the
               client.  This can happen especially if I/O errors occur
               during addTextToStore() / importPath().  If that
               happens, just send the error message and exit. */
            bool errorAllowed = canSendStderr;
            if (!errorAllowed) printMsg(lvlError, format("error processing client input: %1%") % e.msg());
            stopWork(false, e.msg(), GET_PROTOCOL_MINOR(clientVersion) >= 8 ? e.status : 0);
            if (!errorAllowed) break;
        } catch (std::bad_alloc & e) {
            if (canSendStderr)
                stopWork(false, "Nix daemon out of memory", GET_PROTOCOL_MINOR(clientVersion) >= 8 ? 1 : 0);
            throw;
        }

        to.flush();

        assert(!canSendStderr);
    };

    printMsg(lvlError, format("%1% operations") % opCount);
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


#define SD_LISTEN_FDS_START 3


static void daemonLoop()
{
    /* Get rid of children automatically; don't let them become
       zombies. */
    setSigChldAction(true);

    AutoCloseFD fdSocket;

    /* Handle socket-based activation by systemd. */
    if (getEnv("LISTEN_FDS") != "") {
        if (getEnv("LISTEN_PID") != int2String(getpid()) || getEnv("LISTEN_FDS") != "1")
            throw Error("unexpected systemd environment variables");
        fdSocket = SD_LISTEN_FDS_START;
    }

    /* Otherwise, create and bind to a Unix domain socket. */
    else {

        /* Create and bind to a Unix domain socket. */
        fdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
        if (fdSocket == -1)
            throw SysError("cannot create Unix domain socket");

        string socketPath = settings.nixDaemonSocketFile;

        createDirs(dirOf(socketPath));

        /* Urgh, sockaddr_un allows path names of only 108 characters.
           So chdir to the socket directory so that we can pass a
           relative path name. */
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
    }

    closeOnExec(fdSocket);

    /* Loop accepting connections. */
    while (1) {

        try {
            /* Important: the server process *cannot* open the SQLite
               database, because it doesn't like forks very much. */
            assert(!store);

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(fdSocket,
                (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            checkInterrupt();
            if (remote == -1) {
                if (errno == EINTR)
                    continue;
                else
                    throw SysError("accepting connection");
            }

            closeOnExec(remote);

            /* Get the identity of the caller, if possible. */
            uid_t clientUid = -1;
            pid_t clientPid = -1;
            bool trusted = false;

#if defined(SO_PEERCRED)
            ucred cred;
            socklen_t credLen = sizeof(cred);
            if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) != -1) {
                clientPid = cred.pid;
                clientUid = cred.uid;
                if (clientUid == 0) trusted = true;
            }
#endif

            printMsg(lvlInfo, format("accepted connection from pid %1%, uid %2%") % clientPid % clientUid);

            /* Fork a child to handle the connection. */
            pid_t child;
            child = fork();

            switch (child) {

            case -1:
                throw SysError("unable to fork");

            case 0:
                try { /* child */

                    /* Background the daemon. */
                    if (setsid() == -1)
                        throw SysError(format("creating a new session"));

                    /* Restore normal handling of SIGCHLD. */
                    setSigChldAction(false);

                    /* For debugging, stuff the pid into argv[1]. */
                    if (clientPid != -1 && argvSaved[1]) {
                        string processName = int2String(clientPid);
                        strncpy(argvSaved[1], processName.c_str(), strlen(argvSaved[1]));
                    }

                    /* Handle the connection. */
                    from.fd = remote;
                    to.fd = remote;
                    processConnection(trusted);

                } catch (std::exception & e) {
                    writeToStderr("unexpected Nix daemon error: " + string(e.what()) + "\n");
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
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--daemon") /* ignored for backwards compatibility */;
    }

    chdir("/");
    daemonLoop();
}


void printHelp()
{
    showManPage("nix-daemon");
}


string programId = "nix-daemon";
