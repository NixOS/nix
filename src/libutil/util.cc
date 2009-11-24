#include "config.h"

#ifdef __CYGWIN__
#include <windows.h>
#endif

#include <iostream>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstring>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#include "util.hh"


extern char * * environ;


namespace nix {


BaseError::BaseError(const format & f)
{
    err = f.str();
}


BaseError & BaseError::addPrefix(const format & f)
{
    prefix_ = f.str() + prefix_;
    return *this;
}


SysError::SysError(const format & f)
    : Error(format("%1%: %2%") % f.str() % strerror(errno))
    , errNo(errno)
{
}


string getEnv(const string & key, const string & def)
{
    char * value = getenv(key.c_str());
    return value ? string(value) : def;
}


Path absPath(Path path, Path dir)
{
    if (path[0] != '/') {
        if (dir == "") {
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
                throw SysError("cannot get cwd");
            dir = buf;
        }
        path = dir + "/" + path;
    }
    return canonPath(path);
}


Path canonPath(const Path & path, bool resolveSymlinks)
{
    string s;

    if (path[0] != '/')
        throw Error(format("not an absolute path: `%1%'") % path);

    string::const_iterator i = path.begin(), end = path.end();
    string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    while (1) {

        /* Skip slashes. */
        while (i != end && *i == '/') i++;
        if (i == end) break;

        /* Ignore `.'. */
        if (*i == '.' && (i + 1 == end || i[1] == '/'))
            i++;

        /* If `..', delete the last component. */
        else if (*i == '.' && i + 1 < end && i[1] == '.' && 
            (i + 2 == end || i[2] == '/'))
        {
            if (!s.empty()) s.erase(s.rfind('/'));
            i += 2;
        }

        /* Normal component; copy it. */
        else {
            s += '/';
            while (i != end && *i != '/') s += *i++;

            /* If s points to a symlink, resolve it and restart (since
               the symlink target might contain new symlinks). */
            if (resolveSymlinks && isLink(s)) {
                if (++followCount >= maxFollow)
                    throw Error(format("infinite symlink recursion in path `%1%'") % path);
                temp = absPath(readLink(s), dirOf(s))
                    + string(i, end);
                i = temp.begin(); /* restart */
                end = temp.end();
                s = "";
                /* !!! potential for infinite loop */
            }
        }
    }

    return s.empty() ? "/" : s;
}


Path dirOf(const Path & path)
{
    Path::size_type pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name `%1%'") % path);
    return pos == 0 ? "/" : Path(path, 0, pos);
}


string baseNameOf(const Path & path)
{
    Path::size_type pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name `%1%'") % path);
    return string(path, pos + 1);
}


bool pathExists(const Path & path)
{
    int res;
    struct stat st;
    res = lstat(path.c_str(), &st);
    if (!res) return true;
    if (errno != ENOENT && errno != ENOTDIR)
        throw SysError(format("getting status of %1%") % path);
    return false;
}


Path readLink(const Path & path)
{
    checkInterrupt();
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (!S_ISLNK(st.st_mode))
        throw Error(format("`%1%' is not a symlink") % path);
    char buf[st.st_size];
    if (readlink(path.c_str(), buf, st.st_size) != st.st_size)
        throw SysError(format("reading symbolic link `%1%'") % path);
    return string(buf, st.st_size);
}


bool isLink(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    return S_ISLNK(st.st_mode);
}


Strings readDirectory(const Path & path)
{
    Strings names;

    AutoCloseDir dir = opendir(path.c_str());
    if (!dir) throw SysError(format("opening directory `%1%'") % path);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) { /* sic */
        checkInterrupt();
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    if (errno) throw SysError(format("reading directory `%1%'") % path);

    return names;
}


string readFile(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        throw SysError("statting file");
    
    unsigned char * buf = new unsigned char[st.st_size];
    AutoDeleteArray<unsigned char> d(buf);
    readFull(fd, buf, st.st_size);

    return string((char *) buf, st.st_size);
}


string readFile(const Path & path)
{
    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        throw SysError(format("opening file `%1%'") % path);
    return readFile(fd);
}


void writeFile(const Path & path, const string & s)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd == -1)
        throw SysError(format("opening file `%1%'") % path);
    writeFull(fd, (unsigned char *) s.c_str(), s.size());
}


string readLine(int fd)
{
    string s;
    while (1) {
        checkInterrupt();
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


void writeLine(int fd, string s)
{
    s += '\n';
    writeFull(fd, (const unsigned char *) s.c_str(), s.size());
}


static void _computePathSize(const Path & path,
    unsigned long long & bytes, unsigned long long & blocks)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    bytes += st.st_size;
    blocks += st.st_blocks;

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            _computePathSize(path + "/" + *i, bytes, blocks);
    }
}


void computePathSize(const Path & path,
    unsigned long long & bytes, unsigned long long & blocks)
{
    bytes = 0;
    blocks = 0;
    _computePathSize(path, bytes, blocks);
}


static void _deletePath(const Path & path, unsigned long long & bytesFreed,
    unsigned long long & blocksFreed)
{
    checkInterrupt();

    printMsg(lvlVomit, format("%1%") % path);

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (!S_ISDIR(st.st_mode) && st.st_nlink == 1) {
        bytesFreed += st.st_size;
        blocksFreed += st.st_blocks;
    }

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	/* Make the directory writable. */
	if (!(st.st_mode & S_IWUSR)) {
	    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
		throw SysError(format("making `%1%' writable") % path);
	}

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            _deletePath(path + "/" + *i, bytesFreed, blocksFreed);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
}


void deletePath(const Path & path)
{
    unsigned long long dummy1, dummy2;
    deletePath(path, dummy1, dummy2);
}


void deletePath(const Path & path, unsigned long long & bytesFreed,
    unsigned long long & blocksFreed)
{
    startNest(nest, lvlDebug,
        format("recursively deleting path `%1%'") % path);
    bytesFreed = 0;
    blocksFreed = 0;
    _deletePath(path, bytesFreed, blocksFreed);
}


void makePathReadOnly(const Path & path)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (!S_ISLNK(st.st_mode) && (st.st_mode & S_IWUSR)) {
	if (chmod(path.c_str(), st.st_mode & ~S_IWUSR) == -1)
	    throw SysError(format("making `%1%' read-only") % path);
    }

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
	    makePathReadOnly(path + "/" + *i);
    }
}


static Path tempName(Path tmpRoot, const Path & prefix, bool includePid,
    int & counter)
{
    tmpRoot = canonPath(tmpRoot.empty() ? getEnv("TMPDIR", "/tmp") : tmpRoot, true);
    if (includePid)
        return (format("%1%/%2%-%3%-%4%") % tmpRoot % prefix % getpid() % counter++).str();
    else
        return (format("%1%/%2%-%3%") % tmpRoot % prefix % counter++).str();
}


Path createTempDir(const Path & tmpRoot, const Path & prefix,
    bool includePid, bool useGlobalCounter)
{
    static int globalCounter = 0;
    int localCounter = 0;
    int & counter(useGlobalCounter ? globalCounter : localCounter);
    
    while (1) {
        checkInterrupt();
	Path tmpDir = tempName(tmpRoot, prefix, includePid, counter);
	if (mkdir(tmpDir.c_str(), 0777) == 0) {
	    /* Explicitly set the group of the directory.  This is to
	       work around around problems caused by BSD's group
	       ownership semantics (directories inherit the group of
	       the parent).  For instance, the group of /tmp on
	       FreeBSD is "wheel", so all directories created in /tmp
	       will be owned by "wheel"; but if the user is not in
	       "wheel", then "tar" will fail to unpack archives that
	       have the setgid bit set on directories. */
	    if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
		throw SysError(format("setting group of directory `%1%'") % tmpDir);
	    return tmpDir;
	}
	if (errno != EEXIST)
	    throw SysError(format("creating directory `%1%'") % tmpDir);
    }
}


Paths createDirs(const Path & path)
{
    Paths created;
    if (path == "/") return created;
    if (!pathExists(path)) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str(), 0777) == -1)
            throw SysError(format("creating directory `%1%'") % path);
        created.push_back(path);
    }
    return created;
}


void writeStringToFile(const Path & path, const string & s)
{
    AutoCloseFD fd(open(path.c_str(),
        O_CREAT | O_EXCL | O_WRONLY, 0666));
    if (fd == -1)
        throw SysError(format("creating file `%1%'") % path);
    writeFull(fd, (unsigned char *) s.c_str(), s.size());
}


LogType logType = ltPretty;
Verbosity verbosity = lvlInfo;

static int nestingLevel = 0;


Nest::Nest()
{
    nest = false;
}


Nest::~Nest()
{
    close();
}


static string escVerbosity(Verbosity level)
{
    return int2String((int) level);
}


void Nest::open(Verbosity level, const format & f)
{
    if (level <= verbosity) {
        if (logType == ltEscapes)
            std::cerr << "\033[" << escVerbosity(level) << "p"
                      << f.str() << "\n";
        else
            printMsg_(level, f);
        nest = true;
        nestingLevel++;
    }
}


void Nest::close()
{
    if (nest) {
        nestingLevel--;
        if (logType == ltEscapes)
            std::cerr << "\033[q";
        nest = false;
    }
}


void printMsg_(Verbosity level, const format & f)
{
    checkInterrupt();
    if (level > verbosity) return;
    string prefix;
    if (logType == ltPretty)
        for (int i = 0; i < nestingLevel; i++)
            prefix += "|   ";
    else if (logType == ltEscapes && level != lvlInfo)
        prefix = "\033[" + escVerbosity(level) + "s";
    string s = (format("%1%%2%\n") % prefix % f.str()).str();
    writeToStderr((const unsigned char *) s.c_str(), s.size());
}


void warnOnce(bool & haveWarned, const format & f)
{
    if (!haveWarned) {
        printMsg(lvlError, format("warning: %1%") % f.str());
        haveWarned = true;
    }
}


static void defaultWriteToStderr(const unsigned char * buf, size_t count)
{
    writeFull(STDERR_FILENO, buf, count);
}


void (*writeToStderr) (const unsigned char * buf, size_t count) = defaultWriteToStderr;


void readFull(int fd, unsigned char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, (char *) buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw SysError("reading from file");
        }
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}


void writeFull(int fd, const unsigned char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = write(fd, (char *) buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw SysError("writing to file");
        }
        count -= res;
        buf += res;
    }
}


string drainFD(int fd)
{
    string result;
    unsigned char buffer[4096];
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buffer, sizeof buffer);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading from file");
        }
        else if (rd == 0) break;
        else result.append((char *) buffer, rd);
    }
    return result;
}



//////////////////////////////////////////////////////////////////////


AutoDelete::AutoDelete(const string & p, bool recursive) : path(p)
{
    del = true;
    this->recursive = recursive;
}

AutoDelete::~AutoDelete()
{
    try {
        if (del) {
            if (recursive)
                deletePath(path);
            else {
                if (remove(path.c_str()) == -1)
                    throw SysError(format("cannot unlink `%1%'") % path);
            }
        }
    } catch (...) {
        ignoreException();
    }
}

void AutoDelete::cancel()
{
    del = false;
}



//////////////////////////////////////////////////////////////////////


AutoCloseFD::AutoCloseFD()
{
    fd = -1;
}


AutoCloseFD::AutoCloseFD(int fd)
{
    this->fd = fd;
}


AutoCloseFD::AutoCloseFD(const AutoCloseFD & fd)
{
    /* Copying a AutoCloseFD isn't allowed (who should get to close
       it?).  But as a edge case, allow copying of closed
       AutoCloseFDs.  This is necessary due to tiresome reasons
       involving copy constructor use on default object values in STL
       containers (like when you do `map[value]' where value isn't in
       the map yet). */
    this->fd = fd.fd;
    if (this->fd != -1) abort();
}


AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (...) {
        ignoreException();
    }
}


void AutoCloseFD::operator =(int fd)
{
    if (this->fd != fd) close();
    this->fd = fd;
}


AutoCloseFD::operator int() const
{
    return fd;
}


void AutoCloseFD::close()
{
    if (fd != -1) {
        if (::close(fd) == -1)
            /* This should never happen. */
            throw SysError("closing file descriptor");
        fd = -1;
    }
}


bool AutoCloseFD::isOpen()
{
    return fd != -1;
}


/* Pass responsibility for closing this fd to the caller. */
int AutoCloseFD::borrow()
{
    int oldFD = fd;
    fd = -1;
    return oldFD;
}


void Pipe::create()
{
    int fds[2];
    if (pipe(fds) != 0) throw SysError("creating pipe");
    readSide = fds[0];
    writeSide = fds[1];
}



//////////////////////////////////////////////////////////////////////


AutoCloseDir::AutoCloseDir()
{
    dir = 0;
}


AutoCloseDir::AutoCloseDir(DIR * dir)
{
    this->dir = dir;
}


AutoCloseDir::~AutoCloseDir()
{
    if (dir) closedir(dir);
}


void AutoCloseDir::operator =(DIR * dir)
{
    this->dir = dir;
}


AutoCloseDir::operator DIR *()
{
    return dir;
}



//////////////////////////////////////////////////////////////////////


Pid::Pid()
{
    pid = -1;
    separatePG = false;
    killSignal = SIGKILL;
}


Pid::~Pid()
{
    kill();
}


void Pid::operator =(pid_t pid)
{
    if (this->pid != pid) kill();
    this->pid = pid;
    killSignal = SIGKILL; // reset signal to default
}


Pid::operator pid_t()
{
    return pid;
}


void Pid::kill()
{
    if (pid == -1) return;
    
    printMsg(lvlError, format("killing process %1%") % pid);

    /* Send the requested signal to the child.  If it has its own
       process group, send the signal to every process in the child
       process group (which hopefully includes *all* its children). */
    if (::kill(separatePG ? -pid : pid, killSignal) != 0)
        printMsg(lvlError, (SysError(format("killing process %1%") % pid).msg()));

    /* Wait until the child dies, disregarding the exit status. */
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        checkInterrupt();
        if (errno != EINTR) printMsg(lvlError,
            (SysError(format("waiting for process %1%") % pid).msg()));
    }

    pid = -1;
}


int Pid::wait(bool block)
{
    while (1) {
        int status;
        int res = waitpid(pid, &status, block ? 0 : WNOHANG);
        if (res == pid) {
            pid = -1;
            return status;
        }
        if (res == 0 && !block) return -1;
        if (errno != EINTR)
            throw SysError("cannot get child exit status");
        checkInterrupt();
    }
}


void Pid::setSeparatePG(bool separatePG)
{
    this->separatePG = separatePG;
}


void Pid::setKillSignal(int signal)
{
    this->killSignal = signal;
}


void killUser(uid_t uid)
{
    debug(format("killing all processes running under uid `%1%'") % uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

    Pid pid;
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            if (setuid(uid) == -1) abort();

	    while (true) {
		if (kill(-1, SIGKILL) == 0) break;
		if (errno == ESRCH) break; /* no more processes */
		if (errno != EINTR)
		    throw SysError(format("cannot kill processes for uid `%1%'") % uid);
	    }

        } catch (std::exception & e) {
            std::cerr << format("killing processes beloging to uid `%1%': %1%")
                % uid % e.what() << std::endl;
            quickExit(1);
        }
        quickExit(0);
    }
    
    /* parent */
    if (pid.wait(true) != 0)
        throw Error(format("cannot kill processes for uid `%1%'") % uid);

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
}


//////////////////////////////////////////////////////////////////////


string runProgram(Path program, bool searchPath, const Strings & args)
{
    checkInterrupt();
    
    /* Create a pipe. */
    Pipe pipe;
    pipe.create();

    /* Fork. */
    Pid pid;
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            pipe.readSide.close();

            if (dup2(pipe.writeSide, STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");

            std::vector<const char *> cargs; /* careful with c_str()! */
            cargs.push_back(program.c_str());
            for (Strings::const_iterator i = args.begin(); i != args.end(); ++i)
                cargs.push_back(i->c_str());
            cargs.push_back(0);

            if (searchPath)
                execvp(program.c_str(), (char * *) &cargs[0]);
            else
                execv(program.c_str(), (char * *) &cargs[0]);
            throw SysError(format("executing `%1%'") % program);
            
        } catch (std::exception & e) {
            std::cerr << "error: " << e.what() << std::endl;
        }
        quickExit(1);
    }

    /* Parent. */

    pipe.writeSide.close();

    string result = drainFD(pipe.readSide);

    /* Wait for the child to finish. */
    int status = pid.wait(true);
    if (!statusOk(status))
        throw Error(format("program `%1%' %2%")
            % program % statusToString(status));

    return result;
}


void closeMostFDs(const set<int> & exceptions)
{
    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO
            && exceptions.find(fd) == exceptions.end())
            close(fd); /* ignore result */
}


void quickExit(int status)
{
#ifdef __CYGWIN__
    /* Hack for Cygwin: _exit() doesn't seem to work quite right,
       since some Berkeley DB code appears to be called when a child
       exits through _exit() (e.g., because execve() failed).  So call
       the Windows API directly. */
    ExitProcess(status);
#else
    _exit(status);
#endif
}


void setuidCleanup()
{
    /* Don't trust the environment. */
    environ = 0;

    /* Make sure that file descriptors 0, 1, 2 are open. */
    for (int fd = 0; fd <= 2; ++fd) {
        struct stat st;
        if (fstat(fd, &st) == -1) abort();
    }
}


//////////////////////////////////////////////////////////////////////


volatile sig_atomic_t _isInterrupted = 0;

void _interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!std::uncaught_exception()) {
        _isInterrupted = 0;
        throw Interrupted("interrupted by the user");
    }
}



//////////////////////////////////////////////////////////////////////


string packStrings(const Strings & strings)
{
    string d;
    for (Strings::const_iterator i = strings.begin();
         i != strings.end(); ++i)
    {
        unsigned int len = i->size();
        d += len & 0xff;
        d += (len >> 8) & 0xff;
        d += (len >> 16) & 0xff;
        d += (len >> 24) & 0xff;
        d += *i;
    }
    return d;
}

    
Strings unpackStrings(const string & s)
{
    Strings strings;
    
    string::const_iterator i = s.begin();
    
    while (i != s.end()) {

        if (i + 4 > s.end())
            throw Error(format("short db entry: `%1%'") % s);
        
        unsigned int len;
        len = (unsigned char) *i++;
        len |= ((unsigned char) *i++) << 8;
        len |= ((unsigned char) *i++) << 16;
        len |= ((unsigned char) *i++) << 24;

        if (len == 0xffffffff) return strings; /* explicit end-of-list */
        
        if (i + len > s.end())
            throw Error(format("short db entry: `%1%'") % s);

        strings.push_back(string(i, i + len));
        i += len;
    }
    
    return strings;
}


Strings tokenizeString(const string & s, const string & separators)
{
    Strings result;
    string::size_type pos = s.find_first_not_of(separators, 0);
    while (pos != string::npos) {
        string::size_type end = s.find_first_of(separators, pos + 1);
        if (end == string::npos) end = s.size();
        string token(s, pos, end - pos);
        result.push_back(token);
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}


string statusToString(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            return (format("failed with exit code %1%") % WEXITSTATUS(status)).str();
        else if (WIFSIGNALED(status)) {
	    int sig = WTERMSIG(status);
#if HAVE_STRSIGNAL
            const char * description = strsignal(sig);
            return (format("failed due to signal %1% (%2%)") % sig % description).str();
#else
            return (format("failed due to signal %1%") % sig).str();
#endif
	}
        else
            return "died abnormally";
    } else return "succeeded";
}


bool statusOk(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


string int2String(int n)
{
    std::ostringstream str;
    str << n;
    return str.str();
}


bool hasSuffix(const string & s, const string & suffix)
{
    return s.size() >= suffix.size() && string(s, s.size() - suffix.size()) == suffix;
}


void ignoreException()
{
    try {
        throw;
    } catch (std::exception & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.what());
    }
}

 
}
