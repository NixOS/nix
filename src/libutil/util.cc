#include "config.h"

#include "util.hh"
#include "affinity.hh"

#include <iostream>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#ifdef __APPLE__
#include <sys/syscall.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif


extern char * * environ;


namespace nix {


BaseError::BaseError(const FormatOrString & fs, unsigned int status)
    : status(status)
{
    err = fs.s;
}


BaseError & BaseError::addPrefix(const FormatOrString & fs)
{
    prefix_ = fs.s + prefix_;
    return *this;
}


SysError::SysError(const FormatOrString & fs)
    : Error(format("%1%: %2%") % fs.s % strerror(errno))
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
#ifdef __GNU__
            /* GNU (aka. GNU/Hurd) doesn't have any limitation on path
               lengths and doesn't define `PATH_MAX'.  */
            char *buf = getcwd(NULL, 0);
            if (buf == NULL)
#else
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
#endif
                throw SysError("cannot get cwd");
            dir = buf;
#ifdef __GNU__
            free(buf);
#endif
        }
        path = dir + "/" + path;
    }
    return canonPath(path);
}


Path canonPath(const Path & path, bool resolveSymlinks)
{
    string s;

    if (path[0] != '/')
        throw Error(format("not an absolute path: ‘%1%’") % path);

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
                    throw Error(format("infinite symlink recursion in path ‘%1%’") % path);
                temp = absPath(readLink(s), dirOf(s))
                    + string(i, end);
                i = temp.begin(); /* restart */
                end = temp.end();
                s = "";
            }
        }
    }

    return s.empty() ? "/" : s;
}


Path dirOf(const Path & path)
{
    Path::size_type pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name ‘%1%’") % path);
    return pos == 0 ? "/" : Path(path, 0, pos);
}


string baseNameOf(const Path & path)
{
    if (path.empty())
        return string("");

    Path::size_type last = path.length() - 1;
    if (path[last] == '/' && last > 0)
        last -= 1;

    Path::size_type pos = path.rfind('/', last);
    if (pos == string::npos)
        pos = 0;
    else
        pos += 1;
    return string(path, pos, last - pos + 1);
}


bool isInDir(const Path & path, const Path & dir)
{
    return path[0] == '/'
        && string(path, 0, dir.size()) == dir
        && path.size() >= dir.size() + 2
        && path[dir.size()] == '/';
}


struct stat lstat(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting status of ‘%1%’") % path);
    return st;
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
    struct stat st = lstat(path);
    if (!S_ISLNK(st.st_mode))
        throw Error(format("‘%1%’ is not a symlink") % path);
    char buf[st.st_size];
    ssize_t rlsize = readlink(path.c_str(), buf, st.st_size);
    if (rlsize == -1)
        throw SysError(format("reading symbolic link ‘%1%’") % path);
    else if (rlsize > st.st_size)
        throw Error(format("symbolic link ‘%1%’ size overflow %2% > %3%")
            % path % rlsize % st.st_size);
    return string(buf, st.st_size);
}


bool isLink(const Path & path)
{
    struct stat st = lstat(path);
    return S_ISLNK(st.st_mode);
}


DirEntries readDirectory(const Path & path)
{
    DirEntries entries;
    entries.reserve(64);

    AutoCloseDir dir = opendir(path.c_str());
    if (!dir) throw SysError(format("opening directory ‘%1%’") % path);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) { /* sic */
        checkInterrupt();
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        entries.emplace_back(name, dirent->d_ino,
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
            dirent->d_type
#else
            DT_UNKNOWN
#endif
        );
    }
    if (errno) throw SysError(format("reading directory ‘%1%’") % path);

    return entries;
}


unsigned char getFileType(const Path & path)
{
    struct stat st = lstat(path);
    if (S_ISDIR(st.st_mode)) return DT_DIR;
    if (S_ISLNK(st.st_mode)) return DT_LNK;
    if (S_ISREG(st.st_mode)) return DT_REG;
    return DT_UNKNOWN;
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


string readFile(const Path & path, bool drain)
{
    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        throw SysError(format("opening file ‘%1%’") % path);
    return drain ? drainFD(fd) : readFile(fd);
}


void writeFile(const Path & path, const string & s)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd == -1)
        throw SysError(format("opening file ‘%1%’") % path);
    writeFull(fd, s);
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
            throw EndOfFile("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}


void writeLine(int fd, string s)
{
    s += '\n';
    writeFull(fd, s);
}


static void _deletePath(const Path & path, unsigned long long & bytesFreed)
{
    checkInterrupt();

    printMsg(lvlVomit, format("%1%") % path);

    struct stat st = lstat(path);

    if (!S_ISDIR(st.st_mode) && st.st_nlink == 1)
        bytesFreed += st.st_blocks * 512;

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK) {
            if (chmod(path.c_str(), st.st_mode | PERM_MASK) == -1)
                throw SysError(format("chmod ‘%1%’") % path);
        }

        for (auto & i : readDirectory(path))
            _deletePath(path + "/" + i.name, bytesFreed);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink ‘%1%’") % path);
}


void deletePath(const Path & path)
{
    unsigned long long dummy;
    deletePath(path, dummy);
}


void deletePath(const Path & path, unsigned long long & bytesFreed)
{
    startNest(nest, lvlDebug,
        format("recursively deleting path ‘%1%’") % path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed);
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
    bool includePid, bool useGlobalCounter, mode_t mode)
{
    static int globalCounter = 0;
    int localCounter = 0;
    int & counter(useGlobalCounter ? globalCounter : localCounter);

    while (1) {
        checkInterrupt();
        Path tmpDir = tempName(tmpRoot, prefix, includePid, counter);
        if (mkdir(tmpDir.c_str(), mode) == 0) {
            /* Explicitly set the group of the directory.  This is to
               work around around problems caused by BSD's group
               ownership semantics (directories inherit the group of
               the parent).  For instance, the group of /tmp on
               FreeBSD is "wheel", so all directories created in /tmp
               will be owned by "wheel"; but if the user is not in
               "wheel", then "tar" will fail to unpack archives that
               have the setgid bit set on directories. */
            if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
                throw SysError(format("setting group of directory ‘%1%’") % tmpDir);
            return tmpDir;
        }
        if (errno != EEXIST)
            throw SysError(format("creating directory ‘%1%’") % tmpDir);
    }
}


Paths createDirs(const Path & path)
{
    Paths created;
    if (path == "/") return created;

    struct stat st;
    if (lstat(path.c_str(), &st) == -1) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str(), 0777) == -1 && errno != EEXIST)
            throw SysError(format("creating directory ‘%1%’") % path);
        st = lstat(path);
        created.push_back(path);
    }

    if (S_ISLNK(st.st_mode) && stat(path.c_str(), &st) == -1)
        throw SysError(format("statting symlink ‘%1%’") % path);

    if (!S_ISDIR(st.st_mode)) throw Error(format("‘%1%’ is not a directory") % path);

    return created;
}


void createSymlink(const Path & target, const Path & link)
{
    if (symlink(target.c_str(), link.c_str()))
        throw SysError(format("creating symlink from ‘%1%’ to ‘%2%’") % link % target);
}


void replaceSymlink(const Path & target, const Path & link)
{
    for (unsigned int n = 0; true; n++) {
        Path tmp = canonPath((format("%s/.%d_%s") % dirOf(link) % n % baseNameOf(link)).str());

        try {
            createSymlink(target, tmp);
        } catch (SysError & e) {
            if (e.errNo == EEXIST) continue;
            throw;
        }

        if (rename(tmp.c_str(), link.c_str()) != 0)
            throw SysError(format("renaming ‘%1%’ to ‘%2%’") % tmp % link);

        break;
    }
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
    return std::to_string((int) level);
}


void Nest::open(Verbosity level, const FormatOrString & fs)
{
    if (level <= verbosity) {
        if (logType == ltEscapes)
            std::cerr << "\033[" << escVerbosity(level) << "p"
                      << fs.s << "\n";
        else
            printMsg_(level, fs);
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


void printMsg_(Verbosity level, const FormatOrString & fs)
{
    checkInterrupt();
    if (level > verbosity) return;

    string prefix;
    if (logType == ltPretty)
        for (int i = 0; i < nestingLevel; i++)
            prefix += "|   ";
    else if (logType == ltEscapes && level != lvlInfo)
        prefix = "\033[" + escVerbosity(level) + "s";
    else if (logType == ltSystemd) {
        char c;
        switch (level) {
            case lvlError: c = '3'; break;
            case lvlInfo: c = '5'; break;
            case lvlTalkative: case lvlChatty: c = '6'; break;
            default: c = '7';
        }
        prefix = string("<") + c + ">";
    }

    string s = (format("%1%%2%\n") % prefix % fs.s).str();
    if (!isatty(STDERR_FILENO)) s = filterANSIEscapes(s);
    writeToStderr(s);
}


void warnOnce(bool & haveWarned, const FormatOrString & fs)
{
    if (!haveWarned) {
        printMsg(lvlError, format("warning: %1%") % fs.s);
        haveWarned = true;
    }
}


void writeToStderr(const string & s)
{
    try {
        if (_writeToStderr)
            _writeToStderr((const unsigned char *) s.data(), s.size());
        else
            writeFull(STDERR_FILENO, s);
    } catch (SysError & e) {
        /* Ignore failing writes to stderr if we're in an exception
           handler, otherwise throw an exception.  We need to ignore
           write errors in exception handlers to ensure that cleanup
           code runs to completion if the other side of stderr has
           been closed unexpectedly. */
        if (!std::uncaught_exception()) throw;
    }
}


void (*_writeToStderr) (const unsigned char * buf, size_t count) = 0;


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


void writeFull(int fd, const string & s)
{
    writeFull(fd, (const unsigned char *) s.data(), s.size());
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


AutoDelete::AutoDelete() : del{false} {}

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
                    throw SysError(format("cannot unlink ‘%1%’") % path);
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

void AutoDelete::reset(const Path & p, bool recursive) {
    path = p;
    this->recursive = recursive;
    del = true;
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
    /* Copying an AutoCloseFD isn't allowed (who should get to close
       it?).  But as an edge case, allow copying of closed
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
            throw SysError(format("closing file descriptor %1%") % fd);
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
    closeOnExec(readSide);
    closeOnExec(writeSide);
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
    close();
}


void AutoCloseDir::operator =(DIR * dir)
{
    this->dir = dir;
}


AutoCloseDir::operator DIR *()
{
    return dir;
}


void AutoCloseDir::close()
{
    if (dir) {
        closedir(dir);
        dir = 0;
    }
}


//////////////////////////////////////////////////////////////////////


Pid::Pid()
    : pid(-1), separatePG(false), killSignal(SIGKILL)
{
}


Pid::Pid(pid_t pid)
    : pid(pid), separatePG(false), killSignal(SIGKILL)
{
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


void Pid::kill(bool quiet)
{
    if (pid == -1 || pid == 0) return;

    if (!quiet)
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
        if (errno != EINTR) {
            printMsg(lvlError,
                (SysError(format("waiting for process %1%") % pid).msg()));
            break;
        }
    }

    pid = -1;
}


int Pid::wait(bool block)
{
    assert(pid != -1);
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
    debug(format("killing all processes running under uid ‘%1%’") % uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

    ProcessOptions options;
    options.allowVfork = false;

    Pid pid = startProcess([&]() {

        if (setuid(uid) == -1)
            throw SysError("setting uid");

        while (true) {
#ifdef __APPLE__
            /* OSX's kill syscall takes a third parameter that, among
               other things, determines if kill(-1, signo) affects the
               calling process. In the OSX libc, it's set to true,
               which means "follow POSIX", which we don't want here
                 */
            if (syscall(SYS_kill, -1, SIGKILL, false) == 0) break;
#else
            if (kill(-1, SIGKILL) == 0) break;
#endif
            if (errno == ESRCH) break; /* no more processes */
            if (errno != EINTR)
                throw SysError(format("cannot kill processes for uid ‘%1%’") % uid);
        }

        _exit(0);
    }, options);

    int status = pid.wait(true);
    if (status != 0)
        throw Error(format("cannot kill processes for uid ‘%1%’: %2%") % uid % statusToString(status));

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
}


//////////////////////////////////////////////////////////////////////


/* Wrapper around vfork to prevent the child process from clobbering
   the caller's stack frame in the parent. */
static pid_t doFork(bool allowVfork, std::function<void()> fun) __attribute__((noinline));
static pid_t doFork(bool allowVfork, std::function<void()> fun)
{
#ifdef __linux__
    pid_t pid = allowVfork ? vfork() : fork();
#else
    pid_t pid = fork();
#endif
    if (pid != 0) return pid;
    fun();
    abort();
}


pid_t startProcess(std::function<void()> fun, const ProcessOptions & options)
{
    auto wrapper = [&]() {
        if (!options.allowVfork) _writeToStderr = 0;
        try {
#if __linux__
            if (options.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
                throw SysError("setting death signal");
#endif
            restoreAffinity();
            fun();
        } catch (std::exception & e) {
            try {
                std::cerr << options.errorPrefix << e.what() << "\n";
            } catch (...) { }
        } catch (...) { }
        if (options.runExitHandlers)
            exit(1);
        else
            _exit(1);
    };

    pid_t pid = doFork(options.allowVfork, wrapper);
    if (pid == -1) throw SysError("unable to fork");

    return pid;
}


std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    for (auto & s : ss) res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}


string runProgram(Path program, bool searchPath, const Strings & args,
    const string & input)
{
    checkInterrupt();

    /* Create a pipe. */
    Pipe out, in;
    out.create();
    if (!input.empty()) in.create();

    /* Fork. */
    Pid pid = startProcess([&]() {
        if (dup2(out.writeSide, STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
        if (!input.empty()) {
            if (dup2(in.readSide, STDIN_FILENO) == -1)
                throw SysError("dupping stdin");
        }

        Strings args_(args);
        args_.push_front(program);

        if (searchPath)
            execvp(program.c_str(), stringsToCharPtrs(args_).data());
        else
            execv(program.c_str(), stringsToCharPtrs(args_).data());

        throw SysError(format("executing ‘%1%’") % program);
    });

    out.writeSide.close();

    /* FIXME: This can deadlock if the input is too long. */
    if (!input.empty()) {
        in.readSide.close();
        writeFull(in.writeSide, input);
        in.writeSide.close();
    }

    string result = drainFD(out.readSide);

    /* Wait for the child to finish. */
    int status = pid.wait(true);
    if (!statusOk(status))
        throw ExecError(format("program ‘%1%’ %2%")
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


void closeOnExec(int fd)
{
    int prev;
    if ((prev = fcntl(fd, F_GETFD, 0)) == -1 ||
        fcntl(fd, F_SETFD, prev | FD_CLOEXEC) == -1)
        throw SysError("setting close-on-exec flag");
}


void restoreSIGPIPE()
{
    struct sigaction act;
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGPIPE, &act, 0)) throw SysError("resetting SIGPIPE");
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


template<class C> C tokenizeString(const string & s, const string & separators)
{
    C result;
    string::size_type pos = s.find_first_not_of(separators, 0);
    while (pos != string::npos) {
        string::size_type end = s.find_first_of(separators, pos + 1);
        if (end == string::npos) end = s.size();
        string token(s, pos, end - pos);
        result.insert(result.end(), token);
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template Strings tokenizeString(const string & s, const string & separators);
template StringSet tokenizeString(const string & s, const string & separators);
template vector<string> tokenizeString(const string & s, const string & separators);


string concatStringsSep(const string & sep, const Strings & ss)
{
    string s;
    for (auto & i : ss) {
        if (s.size() != 0) s += sep;
        s += i;
    }
    return s;
}


string concatStringsSep(const string & sep, const StringSet & ss)
{
    string s;
    for (auto & i : ss) {
        if (s.size() != 0) s += sep;
        s += i;
    }
    return s;
}


string chomp(const string & s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == string::npos ? "" : string(s, 0, i + 1);
}


string trim(const string & s, const string & whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == string::npos) return "";
    auto j = s.find_last_not_of(whitespace);
    return string(s, i, j == string::npos ? j : j - i + 1);
}


string replaceStrings(const std::string & s,
    const std::string & from, const std::string & to)
{
    if (from.empty()) return s;
    string res = s;
    size_t pos = 0;
    while ((pos = res.find(from, pos)) != std::string::npos) {
        res.replace(pos, from.size(), to);
        pos += to.size();
    }
    return res;
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


bool hasPrefix(const string & s, const string & suffix)
{
    return s.compare(0, suffix.size(), suffix) == 0;
}


bool hasSuffix(const string & s, const string & suffix)
{
    return s.size() >= suffix.size() && string(s, s.size() - suffix.size()) == suffix;
}


void expect(std::istream & str, const string & s)
{
    char s2[s.size()];
    str.read(s2, s.size());
    if (string(s2, s.size()) != s)
        throw FormatError(format("expected string ‘%1%’") % s);
}


string parseString(std::istream & str)
{
    string res;
    expect(str, "\"");
    int c;
    while ((c = str.get()) != '"')
        if (c == '\\') {
            c = str.get();
            if (c == 'n') res += '\n';
            else if (c == 'r') res += '\r';
            else if (c == 't') res += '\t';
            else res += c;
        }
        else res += c;
    return res;
}


bool endOfList(std::istream & str)
{
    if (str.peek() == ',') {
        str.get();
        return false;
    }
    if (str.peek() == ']') {
        str.get();
        return true;
    }
    return false;
}


string decodeOctalEscaped(const string & s)
{
    string r;
    for (string::const_iterator i = s.begin(); i != s.end(); ) {
        if (*i != '\\') { r += *i++; continue; }
        unsigned char c = 0;
        ++i;
        while (i != s.end() && *i >= '0' && *i < '8')
            c = c * 8 + (*i++ - '0');
        r += c;
    }
    return r;
}


void ignoreException()
{
    try {
        throw;
    } catch (std::exception & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.what());
    }
}


string filterANSIEscapes(const string & s, bool nixOnly)
{
    string t, r;
    enum { stTop, stEscape, stCSI } state = stTop;
    for (auto c : s) {
        if (state == stTop) {
            if (c == '\e') {
                state = stEscape;
                r = c;
            } else
                t += c;
        } else if (state == stEscape) {
            r += c;
            if (c == '[')
                state = stCSI;
            else {
                t += r;
                state = stTop;
            }
        } else {
            r += c;
            if (c >= 0x40 && c != 0x7e) {
                if (nixOnly && (c != 'p' && c != 'q' && c != 's' && c != 'a' && c != 'b'))
                    t += r;
                state = stTop;
                r.clear();
            }
        }
    }
    t += r;
    return t;
}


static char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


string base64Encode(const string & s)
{
    string res;
    int data = 0, nbits = 0;

    for (char c : s) {
        data = data << 8 | (unsigned char) c;
        nbits += 8;
        while (nbits >= 6) {
            nbits -= 6;
            res.push_back(base64Chars[data >> nbits & 0x3f]);
        }
    }

    if (nbits) res.push_back(base64Chars[data << (6 - nbits) & 0x3f]);
    while (res.size() % 4) res.push_back('=');

    return res;
}


string base64Decode(const string & s)
{
    bool init = false;
    char decode[256];
    if (!init) {
        // FIXME: not thread-safe.
        memset(decode, -1, sizeof(decode));
        for (int i = 0; i < 64; i++)
            decode[(int) base64Chars[i]] = i;
        init = true;
    }

    string res;
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=') break;
        if (c == '\n') continue;

        char digit = decode[(unsigned char) c];
        if (digit == -1)
            throw Error("invalid character in Base64 string");

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
}


}
