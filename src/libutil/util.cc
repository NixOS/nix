#include "util.hh"
#include "sync.hh"
#include "finally.hh"
#include "serialise.hh"
#include "cgroup.hh"

#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/syscall.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <cmath>
#endif


extern char * * environ __attribute__((weak));


namespace nix {

void initLibUtil() {
}

std::optional<std::string> getEnv(const std::string & key)
{
    char * value = getenv(key.c_str());
    if (!value) return {};
    return std::string(value);
}

std::optional<std::string> getEnvNonEmpty(const std::string & key) {
    auto value = getEnv(key);
    if (value == "") return {};
    return value;
}

std::map<std::string, std::string> getEnv()
{
    std::map<std::string, std::string> env;
    for (size_t i = 0; environ[i]; ++i) {
        auto s = environ[i];
        auto eq = strchr(s, '=');
        if (!eq)
            // invalid env, just keep going
            continue;
        env.emplace(std::string(s, eq), std::string(eq + 1));
    }
    return env;
}


void clearEnv()
{
    for (auto & name : getEnv())
        unsetenv(name.first.c_str());
}

void replaceEnv(const std::map<std::string, std::string> & newEnv)
{
    clearEnv();
    for (auto & newEnvVar : newEnv)
        setenv(newEnvVar.first.c_str(), newEnvVar.second.c_str(), 1);
}


Path absPath(Path path, std::optional<PathView> dir, bool resolveSymlinks)
{
    if (path[0] != '/') {
        if (!dir) {
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
            path = concatStrings(buf, "/", path);
#ifdef __GNU__
            free(buf);
#endif
        } else
            path = concatStrings(*dir, "/", path);
    }
    return canonPath(path, resolveSymlinks);
}


Path canonPath(PathView path, bool resolveSymlinks)
{
    assert(path != "");

    std::string s;
    s.reserve(256);

    if (path[0] != '/')
        throw Error("not an absolute path: '%1%'", path);

    std::string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    while (1) {

        /* Skip slashes. */
        while (!path.empty() && path[0] == '/') path.remove_prefix(1);
        if (path.empty()) break;

        /* Ignore `.'. */
        if (path == "." || path.substr(0, 2) == "./")
            path.remove_prefix(1);

        /* If `..', delete the last component. */
        else if (path == ".." || path.substr(0, 3) == "../")
        {
            if (!s.empty()) s.erase(s.rfind('/'));
            path.remove_prefix(2);
        }

        /* Normal component; copy it. */
        else {
            s += '/';
            if (const auto slash = path.find('/'); slash == std::string::npos) {
                s += path;
                path = {};
            } else {
                s += path.substr(0, slash);
                path = path.substr(slash);
            }

            /* If s points to a symlink, resolve it and continue from there */
            if (resolveSymlinks && isLink(s)) {
                if (++followCount >= maxFollow)
                    throw Error("infinite symlink recursion in path '%1%'", path);
                temp = concatStrings(readLink(s), path);
                path = temp;
                if (!temp.empty() && temp[0] == '/') {
                    s.clear();  /* restart for symlinks pointing to absolute path */
                } else {
                    s = dirOf(s);
                    if (s == "/") {  // we don’t want trailing slashes here, which dirOf only produces if s = /
                        s.clear();
                    }
                }
            }
        }
    }

    return s.empty() ? "/" : std::move(s);
}


Path dirOf(const PathView path)
{
    Path::size_type pos = path.rfind('/');
    if (pos == std::string::npos)
        return ".";
    return pos == 0 ? "/" : Path(path, 0, pos);
}


std::string_view baseNameOf(std::string_view path)
{
    if (path.empty())
        return "";

    auto last = path.size() - 1;
    if (path[last] == '/' && last > 0)
        last -= 1;

    auto pos = path.rfind('/', last);
    if (pos == std::string::npos)
        pos = 0;
    else
        pos += 1;

    return path.substr(pos, last - pos + 1);
}


std::string expandTilde(std::string_view path)
{
    // TODO: expand ~user ?
    auto tilde = path.substr(0, 2);
    if (tilde == "~/" || tilde == "~")
        return getHome() + std::string(path.substr(1));
    else
        return std::string(path);
}


bool isInDir(std::string_view path, std::string_view dir)
{
    return path.substr(0, 1) == "/"
        && path.substr(0, dir.size()) == dir
        && path.size() >= dir.size() + 2
        && path[dir.size()] == '/';
}


bool isDirOrInDir(std::string_view path, std::string_view dir)
{
    return path == dir || isInDir(path, dir);
}


struct stat stat(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}


struct stat lstat(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}


bool pathExists(const Path & path)
{
    int res;
    struct stat st;
    res = lstat(path.c_str(), &st);
    if (!res) return true;
    if (errno != ENOENT && errno != ENOTDIR)
        throw SysError("getting status of %1%", path);
    return false;
}

bool pathAccessible(const Path & path)
{
    try {
        return pathExists(path);
    } catch (SysError & e) {
        // swallow EPERM
        if (e.errNo == EPERM) return false;
        throw;
    }
}


Path readLink(const Path & path)
{
    checkInterrupt();
    std::vector<char> buf;
    for (ssize_t bufSize = PATH_MAX/4; true; bufSize += bufSize/2) {
        buf.resize(bufSize);
        ssize_t rlSize = readlink(path.c_str(), buf.data(), bufSize);
        if (rlSize == -1)
            if (errno == EINVAL)
                throw Error("'%1%' is not a symlink", path);
            else
                throw SysError("reading symbolic link '%1%'", path);
        else if (rlSize < bufSize)
            return std::string(buf.data(), rlSize);
    }
}


bool isLink(const Path & path)
{
    struct stat st = lstat(path);
    return S_ISLNK(st.st_mode);
}


DirEntries readDirectory(DIR *dir, const Path & path)
{
    DirEntries entries;
    entries.reserve(64);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) { /* sic */
        checkInterrupt();
        std::string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        entries.emplace_back(name, dirent->d_ino,
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
            dirent->d_type
#else
            DT_UNKNOWN
#endif
        );
    }
    if (errno) throw SysError("reading directory '%1%'", path);

    return entries;
}

DirEntries readDirectory(const Path & path)
{
    AutoCloseDir dir(opendir(path.c_str()));
    if (!dir) throw SysError("opening directory '%1%'", path);

    return readDirectory(dir.get(), path);
}


unsigned char getFileType(const Path & path)
{
    struct stat st = lstat(path);
    if (S_ISDIR(st.st_mode)) return DT_DIR;
    if (S_ISLNK(st.st_mode)) return DT_LNK;
    if (S_ISREG(st.st_mode)) return DT_REG;
    return DT_UNKNOWN;
}


std::string readFile(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        throw SysError("statting file");

    return drainFD(fd, true, st.st_size);
}


std::string readFile(const Path & path)
{
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd)
        throw SysError("opening file '%1%'", path);
    return readFile(fd.get());
}


void readFile(const Path & path, Sink & sink)
{
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd)
        throw SysError("opening file '%s'", path);
    drainFD(fd.get(), sink);
}


void writeFile(const Path & path, std::string_view s, mode_t mode, bool sync)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode);
    if (!fd)
        throw SysError("opening file '%1%'", path);
    try {
        writeFull(fd.get(), s);
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    if (sync)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync)
        syncParent(path);
}


void writeFile(const Path & path, Source & source, mode_t mode, bool sync)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode);
    if (!fd)
        throw SysError("opening file '%1%'", path);

    std::vector<char> buf(64 * 1024);

    try {
        while (true) {
            try {
                auto n = source.read(buf.data(), buf.size());
                writeFull(fd.get(), {buf.data(), n});
            } catch (EndOfFile &) { break; }
        }
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    if (sync)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync)
        syncParent(path);
}

void syncParent(const Path & path)
{
    AutoCloseFD fd = open(dirOf(path).c_str(), O_RDONLY, 0);
    if (!fd)
        throw SysError("opening file '%1%'", path);
    fd.fsync();
}

std::string readLine(int fd)
{
    std::string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
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


void writeLine(int fd, std::string s)
{
    s += '\n';
    writeFull(fd, s);
}


static void _deletePath(int parentfd, const Path & path, uint64_t & bytesFreed)
{
    checkInterrupt();

    std::string name(baseNameOf(path));

    struct stat st;
    if (fstatat(parentfd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT) return;
        throw SysError("getting status of '%1%'", path);
    }

    if (!S_ISDIR(st.st_mode)) {
        /* We are about to delete a file. Will it likely free space? */

        switch (st.st_nlink) {
            /* Yes: last link. */
            case 1:
                bytesFreed += st.st_size;
                break;
            /* Maybe: yes, if 'auto-optimise-store' or manual optimisation
               was performed. Instead of checking for real let's assume
               it's an optimised file and space will be freed.

               In worst case we will double count on freed space for files
               with exactly two hardlinks for unoptimised packages.
             */
            case 2:
                bytesFreed += st.st_size;
                break;
            /* No: 3+ links. */
            default:
                break;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK) {
            if (fchmodat(parentfd, name.c_str(), st.st_mode | PERM_MASK, 0) == -1)
                throw SysError("chmod '%1%'", path);
        }

        int fd = openat(parentfd, path.c_str(), O_RDONLY);
        if (fd == -1)
            throw SysError("opening directory '%1%'", path);
        AutoCloseDir dir(fdopendir(fd));
        if (!dir)
            throw SysError("opening directory '%1%'", path);
        for (auto & i : readDirectory(dir.get(), path))
            _deletePath(dirfd(dir.get()), path + "/" + i.name, bytesFreed);
    }

    int flags = S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0;
    if (unlinkat(parentfd, name.c_str(), flags) == -1) {
        if (errno == ENOENT) return;
        throw SysError("cannot unlink '%1%'", path);
    }
}

static void _deletePath(const Path & path, uint64_t & bytesFreed)
{
    Path dir = dirOf(path);
    if (dir == "")
        dir = "/";

    AutoCloseFD dirfd{open(dir.c_str(), O_RDONLY)};
    if (!dirfd) {
        if (errno == ENOENT) return;
        throw SysError("opening directory '%1%'", path);
    }

    _deletePath(dirfd.get(), path, bytesFreed);
}


void deletePath(const Path & path)
{
    uint64_t dummy;
    deletePath(path, dummy);
}


void deletePath(const Path & path, uint64_t & bytesFreed)
{
    //Activity act(*logger, lvlDebug, "recursively deleting path '%1%'", path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed);
}


std::string getUserName()
{
    auto pw = getpwuid(geteuid());
    std::string name = pw ? pw->pw_name : getEnv("USER").value_or("");
    if (name.empty())
        throw Error("cannot figure out user name");
    return name;
}

Path getHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0
        || !pw || !pw->pw_dir || !pw->pw_dir[0])
        throw Error("cannot determine user's home directory");
    return pw->pw_dir;
}

Path getHome()
{
    static Path homeDir = []()
    {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use $HOME if doesn't exist or is owned by the current user.
            struct stat st;
            int result = stat(homeDir->c_str(), &st);
            if (result != 0) {
                if (errno != ENOENT) {
                    warn("couldn't stat $HOME ('%s') for reason other than not existing ('%d'), falling back to the one defined in the 'passwd' file", *homeDir, errno);
                    homeDir.reset();
                }
            } else if (st.st_uid != geteuid()) {
                unownedUserHomeDir.swap(homeDir);
            }
        }
        if (!homeDir) {
            homeDir = getHomeOf(geteuid());
            if (unownedUserHomeDir.has_value() && unownedUserHomeDir != homeDir) {
                warn("$HOME ('%s') is not owned by you, falling back to the one defined in the 'passwd' file ('%s')", *unownedUserHomeDir, *homeDir);
            }
        }
        return *homeDir;
    }();
    return homeDir;
}


Path getCacheDir()
{
    auto cacheDir = getEnv("XDG_CACHE_HOME");
    return cacheDir ? *cacheDir : getHome() + "/.cache";
}


Path getConfigDir()
{
    auto configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() + "/.config";
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    result.insert(result.begin(), configHome);
    return result;
}


Path getDataDir()
{
    auto dataDir = getEnv("XDG_DATA_HOME");
    return dataDir ? *dataDir : getHome() + "/.local/share";
}

Path getStateDir()
{
    auto stateDir = getEnv("XDG_STATE_HOME");
    return stateDir ? *stateDir : getHome() + "/.local/state";
}

Path createNixStateDir()
{
    Path dir = getStateDir() + "/nix";
    createDirs(dir);
    return dir;
}


std::optional<Path> getSelfExe()
{
    static auto cached = []() -> std::optional<Path>
    {
        #if __linux__
        return readLink("/proc/self/exe");
        #elif __APPLE__
        char buf[1024];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return buf;
        else
            return std::nullopt;
        #else
        return std::nullopt;
        #endif
    }();
    return cached;
}


Paths createDirs(const Path & path)
{
    Paths created;
    if (path == "/") return created;

    struct stat st;
    if (lstat(path.c_str(), &st) == -1) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str(), 0777) == -1 && errno != EEXIST)
            throw SysError("creating directory '%1%'", path);
        st = lstat(path);
        created.push_back(path);
    }

    if (S_ISLNK(st.st_mode) && stat(path.c_str(), &st) == -1)
        throw SysError("statting symlink '%1%'", path);

    if (!S_ISDIR(st.st_mode)) throw Error("'%1%' is not a directory", path);

    return created;
}


void readFull(int fd, char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw SysError("reading from file");
        }
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}


void writeFull(int fd, std::string_view s, bool allowInterrupts)
{
    while (!s.empty()) {
        if (allowInterrupts) checkInterrupt();
        ssize_t res = write(fd, s.data(), s.size());
        if (res == -1 && errno != EINTR)
            throw SysError("writing to file");
        if (res > 0)
            s.remove_prefix(res);
    }
}


std::string drainFD(int fd, bool block, const size_t reserveSize)
{
    // the parser needs two extra bytes to append terminating characters, other users will
    // not care very much about the extra memory.
    StringSink sink(reserveSize + 2);
    drainFD(fd, sink, block);
    return std::move(sink.s);
}


void drainFD(int fd, Sink & sink, bool block)
{
    // silence GCC maybe-uninitialized warning in finally
    int saved = 0;

    if (!block) {
        saved = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, saved | O_NONBLOCK) == -1)
            throw SysError("making file descriptor non-blocking");
    }

    Finally finally([&]() {
        if (!block) {
            if (fcntl(fd, F_SETFL, saved) == -1)
                throw SysError("making file descriptor blocking");
        }
    });

    std::vector<unsigned char> buf(64 * 1024);
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buf.data(), buf.size());
        if (rd == -1) {
            if (!block && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (errno != EINTR)
                throw SysError("reading from file");
        }
        else if (rd == 0) break;
        else sink({(char *) buf.data(), (size_t) rd});
    }
}

//////////////////////////////////////////////////////////////////////

unsigned int getMaxCPU()
{
    #if __linux__
    try {
        auto cgroupFS = getCgroupFS();
        if (!cgroupFS) return 0;

        auto cgroups = getCgroups("/proc/self/cgroup");
        auto cgroup = cgroups[""];
        if (cgroup == "") return 0;

        auto cpuFile = *cgroupFS + "/" + cgroup + "/cpu.max";

        auto cpuMax = readFile(cpuFile);
        auto cpuMaxParts = tokenizeString<std::vector<std::string>>(cpuMax, " \n");
        auto quota = cpuMaxParts[0];
        auto period = cpuMaxParts[1];
        if (quota != "max")
                return std::ceil(std::stoi(quota) / std::stof(period));
    } catch (Error &) { ignoreException(lvlDebug); }
    #endif

    return 0;
}

//////////////////////////////////////////////////////////////////////


AutoDelete::AutoDelete() : del{false} {}

AutoDelete::AutoDelete(const std::string & p, bool recursive) : path(p)
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
                    throw SysError("cannot unlink '%1%'", path);
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


AutoCloseFD::AutoCloseFD() : fd{-1} {}


AutoCloseFD::AutoCloseFD(int fd) : fd{fd} {}


AutoCloseFD::AutoCloseFD(AutoCloseFD && that) : fd{that.fd}
{
    that.fd = -1;
}


AutoCloseFD & AutoCloseFD::operator =(AutoCloseFD && that)
{
    close();
    fd = that.fd;
    that.fd = -1;
    return *this;
}


AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (...) {
        ignoreException();
    }
}


int AutoCloseFD::get() const
{
    return fd;
}


void AutoCloseFD::close()
{
    if (fd != -1) {
        if (::close(fd) == -1)
            /* This should never happen. */
            throw SysError("closing file descriptor %1%", fd);
        fd = -1;
    }
}

void AutoCloseFD::fsync()
{
  if (fd != -1) {
      int result;
#if __APPLE__
      result = ::fcntl(fd, F_FULLFSYNC);
#else
      result = ::fsync(fd);
#endif
      if (result == -1)
          throw SysError("fsync file descriptor %1%", fd);
  }
}


AutoCloseFD::operator bool() const
{
    return fd != -1;
}


int AutoCloseFD::release()
{
    int oldFD = fd;
    fd = -1;
    return oldFD;
}


void Pipe::create()
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC) != 0) throw SysError("creating pipe");
#else
    if (pipe(fds) != 0) throw SysError("creating pipe");
    closeOnExec(fds[0]);
    closeOnExec(fds[1]);
#endif
    readSide = fds[0];
    writeSide = fds[1];
}


void Pipe::close()
{
    readSide.close();
    writeSide.close();
}


//////////////////////////////////////////////////////////////////////


Pid::Pid()
{
}


Pid::Pid(pid_t pid)
    : pid(pid)
{
}


Pid::~Pid()
{
    if (pid != -1) kill();
}


void Pid::operator =(pid_t pid)
{
    if (this->pid != -1 && this->pid != pid) kill();
    this->pid = pid;
    killSignal = SIGKILL; // reset signal to default
}


Pid::operator pid_t()
{
    return pid;
}


int Pid::kill()
{
    assert(pid != -1);

    debug("killing process %1%", pid);

    /* Send the requested signal to the child.  If it has its own
       process group, send the signal to every process in the child
       process group (which hopefully includes *all* its children). */
    if (::kill(separatePG ? -pid : pid, killSignal) != 0) {
        /* On BSDs, killing a process group will return EPERM if all
           processes in the group are zombies (or something like
           that). So try to detect and ignore that situation. */
#if __FreeBSD__ || __APPLE__
        if (errno != EPERM || ::kill(pid, 0) != 0)
#endif
            logError(SysError("killing process %d", pid).info());
    }

    return wait();
}


int Pid::wait()
{
    assert(pid != -1);
    while (1) {
        int status;
        int res = waitpid(pid, &status, 0);
        if (res == pid) {
            pid = -1;
            return status;
        }
        if (errno != EINTR)
            throw SysError("cannot get exit status of PID %d", pid);
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


pid_t Pid::release()
{
    pid_t p = pid;
    pid = -1;
    return p;
}


void killUser(uid_t uid)
{
    debug("killing all processes running under uid '%1%'", uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

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
            if (errno == ESRCH || errno == EPERM) break; /* no more processes */
            if (errno != EINTR)
                throw SysError("cannot kill processes for uid '%1%'", uid);
        }

        _exit(0);
    });

    int status = pid.wait();
    if (status != 0)
        throw Error("cannot kill processes for uid '%1%': %2%", uid, statusToString(status));

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


#if __linux__
static int childEntry(void * arg)
{
    auto main = (std::function<void()> *) arg;
    (*main)();
    return 1;
}
#endif


pid_t startProcess(std::function<void()> fun, const ProcessOptions & options)
{
    std::function<void()> wrapper = [&]() {
        if (!options.allowVfork)
            logger = makeSimpleLogger();
        try {
#if __linux__
            if (options.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
                throw SysError("setting death signal");
#endif
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

    pid_t pid = -1;

    if (options.cloneFlags) {
        #ifdef __linux__
        // Not supported, since then we don't know when to free the stack.
        assert(!(options.cloneFlags & CLONE_VM));

        size_t stackSize = 1 * 1024 * 1024;
        auto stack = (char *) mmap(0, stackSize,
            PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (stack == MAP_FAILED) throw SysError("allocating stack");

        Finally freeStack([&]() { munmap(stack, stackSize); });

        pid = clone(childEntry, stack + stackSize, options.cloneFlags | SIGCHLD, &wrapper);
        #else
        throw Error("clone flags are only supported on Linux");
        #endif
    } else
        pid = doFork(options.allowVfork, wrapper);

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

std::string runProgram(Path program, bool searchPath, const Strings & args,
    const std::optional<std::string> & input, bool isInteractive)
{
    auto res = runProgram(RunOptions {.program = program, .searchPath = searchPath, .args = args, .input = input, .isInteractive = isInteractive});

    if (!statusOk(res.first))
        throw ExecError(res.first, "program '%1%' %2%", program, statusToString(res.first));

    return res.second;
}

// Output = error code + "standard out" output stream
std::pair<int, std::string> runProgram(RunOptions && options)
{
    StringSink sink;
    options.standardOut = &sink;

    int status = 0;

    try {
        runProgram2(options);
    } catch (ExecError & e) {
        status = e.status;
    }

    return {status, std::move(sink.s)};
}

void runProgram2(const RunOptions & options)
{
    checkInterrupt();

    assert(!(options.standardIn && options.input));

    std::unique_ptr<Source> source_;
    Source * source = options.standardIn;

    if (options.input) {
        source_ = std::make_unique<StringSource>(*options.input);
        source = source_.get();
    }

    /* Create a pipe. */
    Pipe out, in;
    if (options.standardOut) out.create();
    if (source) in.create();

    ProcessOptions processOptions;
    // vfork implies that the environment of the main process and the fork will
    // be shared (technically this is undefined, but in practice that's the
    // case), so we can't use it if we alter the environment
    processOptions.allowVfork = !options.environment;

    std::optional<Finally<std::function<void()>>> resumeLoggerDefer;
    if (options.isInteractive) {
        logger->pause();
        resumeLoggerDefer.emplace(
            []() {
                logger->resume();
            }
        );
    }

    /* Fork. */
    Pid pid = startProcess([&]() {
        if (options.environment)
            replaceEnv(*options.environment);
        if (options.standardOut && dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
        if (options.mergeStderrToStdout)
            if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
                throw SysError("cannot dup stdout into stderr");
        if (source && dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");

        if (options.chdir && chdir((*options.chdir).c_str()) == -1)
            throw SysError("chdir failed");
        if (options.gid && setgid(*options.gid) == -1)
            throw SysError("setgid failed");
        /* Drop all other groups if we're setgid. */
        if (options.gid && setgroups(0, 0) == -1)
            throw SysError("setgroups failed");
        if (options.uid && setuid(*options.uid) == -1)
            throw SysError("setuid failed");

        Strings args_(options.args);
        args_.push_front(options.program);

        restoreProcessContext();

        if (options.searchPath)
            execvp(options.program.c_str(), stringsToCharPtrs(args_).data());
            // This allows you to refer to a program with a pathname relative
            // to the PATH variable.
        else
            execv(options.program.c_str(), stringsToCharPtrs(args_).data());

        throw SysError("executing '%1%'", options.program);
    }, processOptions);

    out.writeSide.close();

    std::thread writerThread;

    std::promise<void> promise;

    Finally doJoin([&]() {
        if (writerThread.joinable())
            writerThread.join();
    });


    if (source) {
        in.readSide.close();
        writerThread = std::thread([&]() {
            try {
                std::vector<char> buf(8 * 1024);
                while (true) {
                    size_t n;
                    try {
                        n = source->read(buf.data(), buf.size());
                    } catch (EndOfFile &) {
                        break;
                    }
                    writeFull(in.writeSide.get(), {buf.data(), n});
                }
                promise.set_value();
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
            in.writeSide.close();
        });
    }

    if (options.standardOut)
        drainFD(out.readSide.get(), *options.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();

    /* Wait for the writer thread to finish. */
    if (source) promise.get_future().get();

    if (status)
        throw ExecError(status, "program '%1%' %2%", options.program, statusToString(status));
}


void closeMostFDs(const std::set<int> & exceptions)
{
#if __linux__
    try {
        for (auto & s : readDirectory("/proc/self/fd")) {
            auto fd = std::stoi(s.name);
            if (!exceptions.count(fd)) {
                debug("closing leaked FD %d", fd);
                close(fd);
            }
        }
        return;
    } catch (SysError &) {
    }
#endif

    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (!exceptions.count(fd))
            close(fd); /* ignore result */
}


void closeOnExec(int fd)
{
    int prev;
    if ((prev = fcntl(fd, F_GETFD, 0)) == -1 ||
        fcntl(fd, F_SETFD, prev | FD_CLOEXEC) == -1)
        throw SysError("setting close-on-exec flag");
}


//////////////////////////////////////////////////////////////////////


std::atomic<bool> _isInterrupted = false;

static thread_local bool interruptThrown = false;
thread_local std::function<bool()> interruptCheck;

void setInterruptThrown()
{
    interruptThrown = true;
}

void _interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!interruptThrown && !std::uncaught_exceptions()) {
        interruptThrown = true;
        throw Interrupted("interrupted by the user");
    }
}


//////////////////////////////////////////////////////////////////////


template<class C> C tokenizeString(std::string_view s, std::string_view separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != std::string_view::npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == std::string_view::npos) end = s.size();
        result.insert(result.end(), std::string(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template Strings tokenizeString(std::string_view s, std::string_view separators);
template StringSet tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);


std::string chomp(std::string_view s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == std::string_view::npos ? "" : std::string(s, 0, i + 1);
}


std::string trim(std::string_view s, std::string_view whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == s.npos) return "";
    auto j = s.find_last_not_of(whitespace);
    return std::string(s, i, j == s.npos ? j : j - i + 1);
}


std::string replaceStrings(
    std::string res,
    std::string_view from,
    std::string_view to)
{
    if (from.empty()) return res;
    size_t pos = 0;
    while ((pos = res.find(from, pos)) != std::string::npos) {
        res.replace(pos, from.size(), to);
        pos += to.size();
    }
    return res;
}


std::string rewriteStrings(std::string s, const StringMap & rewrites)
{
    for (auto & i : rewrites) {
        if (i.first == i.second) continue;
        size_t j = 0;
        while ((j = s.find(i.first, j)) != std::string::npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}


std::string statusToString(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            return fmt("failed with exit code %1%", WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
#if HAVE_STRSIGNAL
            const char * description = strsignal(sig);
            return fmt("failed due to signal %1% (%2%)", sig, description);
#else
            return fmt("failed due to signal %1%", sig);
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


bool hasPrefix(std::string_view s, std::string_view prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}


bool hasSuffix(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size()
        && s.substr(s.size() - suffix.size()) == suffix;
}


std::string toLower(const std::string & s)
{
    std::string r(s);
    for (auto & c : r)
        c = std::tolower(c);
    return r;
}


std::string shellEscape(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += "'";
    for (auto & i : s)
        if (i == '\'') r += "'\\''"; else r += i;
    r += '\'';
    return r;
}


void ignoreException(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (std::exception & e) {
            printMsg(lvl, "error (ignored): %1%", e.what());
        }
    } catch (...) { }
}

bool shouldANSI()
{
    return isatty(STDERR_FILENO)
        && getEnv("TERM").value_or("dumb") != "dumb"
        && !getEnv("NO_COLOR").has_value();
}

std::string filterANSIEscapes(std::string_view s, bool filterAll, unsigned int width)
{
    std::string t, e;
    size_t w = 0;
    auto i = s.begin();

    while (w < (size_t) width && i != s.end()) {

        if (*i == '\e') {
            std::string e;
            e += *i++;
            char last = 0;

            if (i != s.end() && *i == '[') {
                e += *i++;
                // eat parameter bytes
                while (i != s.end() && *i >= 0x30 && *i <= 0x3f) e += *i++;
                // eat intermediate bytes
                while (i != s.end() && *i >= 0x20 && *i <= 0x2f) e += *i++;
                // eat final byte
                if (i != s.end() && *i >= 0x40 && *i <= 0x7e) e += last = *i++;
            } else {
                if (i != s.end() && *i >= 0x40 && *i <= 0x5f) e += *i++;
            }

            if (!filterAll && last == 'm')
                t += e;
        }

        else if (*i == '\t') {
            i++; t += ' '; w++;
            while (w < (size_t) width && w % 8) {
                t += ' '; w++;
            }
        }

        else if (*i == '\r' || *i == '\a')
            // do nothing for now
            i++;

        else {
            w++;
            // Copy one UTF-8 character.
            if ((*i & 0xe0) == 0xc0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
            } else if ((*i & 0xf0) == 0xe0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                }
            } else if ((*i & 0xf8) == 0xf0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                        t += *i++;
                        if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                    }
                }
            } else
                t += *i++;
        }
    }

    return t;
}


constexpr char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view s)
{
    std::string res;
    res.reserve((s.size() + 2) / 3 * 4);
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


std::string base64Decode(std::string_view s)
{
    constexpr char npos = -1;
    constexpr std::array<char, 256> base64DecodeChars = [&]() {
        std::array<char, 256>  result{};
        for (auto& c : result)
            c = npos;
        for (int i = 0; i < 64; i++)
            result[base64Chars[i]] = i;
        return result;
    }();

    std::string res;
    // Some sequences are missing the padding consisting of up to two '='.
    //                    vvv
    res.reserve((s.size() + 2) / 4 * 3);
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=') break;
        if (c == '\n') continue;

        char digit = base64DecodeChars[(unsigned char) c];
        if (digit == npos)
            throw Error("invalid character in Base64 string: '%c'", c);

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
}


std::string stripIndentation(std::string_view s)
{
    size_t minIndent = 10000;
    size_t curIndent = 0;
    bool atStartOfLine = true;

    for (auto & c : s) {
        if (atStartOfLine && c == ' ')
            curIndent++;
        else if (c == '\n') {
            if (atStartOfLine)
                minIndent = std::max(minIndent, curIndent);
            curIndent = 0;
            atStartOfLine = true;
        } else {
            if (atStartOfLine) {
                minIndent = std::min(minIndent, curIndent);
                atStartOfLine = false;
            }
        }
    }

    std::string res;

    size_t pos = 0;
    while (pos < s.size()) {
        auto eol = s.find('\n', pos);
        if (eol == s.npos) eol = s.size();
        if (eol - pos > minIndent)
            res.append(s.substr(pos + minIndent, eol - pos - minIndent));
        res.push_back('\n');
        pos = eol + 1;
    }

    return res;
}


std::pair<std::string_view, std::string_view> getLine(std::string_view s)
{
    auto newline = s.find('\n');

    if (newline == s.npos) {
        return {s, ""};
    } else {
        auto line = s.substr(0, newline);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        return {line, s.substr(newline + 1)};
    }
}


//////////////////////////////////////////////////////////////////////

static Sync<std::pair<unsigned short, unsigned short>> windowSize{{0, 0}};


static void updateWindowSize()
{
    struct winsize ws;
    if (ioctl(2, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize.lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
}


std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize.lock();
}


/* We keep track of interrupt callbacks using integer tokens, so we can iterate
   safely without having to lock the data structure while executing arbitrary
   functions.
 */
struct InterruptCallbacks {
    typedef int64_t Token;

    /* We use unique tokens so that we can't accidentally delete the wrong
       handler because of an erroneous double delete. */
    Token nextToken = 0;

    /* Used as a list, see InterruptCallbacks comment. */
    std::map<Token, std::function<void()>> callbacks;
};

static Sync<InterruptCallbacks> _interruptCallbacks;

static void signalHandlerThread(sigset_t set)
{
    while (true) {
        int signal = 0;
        sigwait(&set, &signal);

        if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP)
            triggerInterrupt();

        else if (signal == SIGWINCH) {
            updateWindowSize();
        }
    }
}

void triggerInterrupt()
{
    _isInterrupted = true;

    {
        InterruptCallbacks::Token i = 0;
        while (true) {
            std::function<void()> callback;
            {
                auto interruptCallbacks(_interruptCallbacks.lock());
                auto lb = interruptCallbacks->callbacks.lower_bound(i);
                if (lb == interruptCallbacks->callbacks.end())
                    break;

                callback = lb->second;
                i = lb->first + 1;
            }

            try {
                callback();
            } catch (...) {
                ignoreException();
            }
        }
    }
}

static sigset_t savedSignalMask;
static bool savedSignalMaskIsSet = false;

void setChildSignalMask(sigset_t * sigs)
{
    assert(sigs); // C style function, but think of sigs as a reference

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
    sigemptyset(&savedSignalMask);
    // There's no "assign" or "copy" function, so we rely on (math) idempotence
    // of the or operator: a or a = a.
    sigorset(&savedSignalMask, sigs, sigs);
#else
    // Without sigorset, our best bet is to assume that sigset_t is a type that
    // can be assigned directly, such as is the case for a sigset_t defined as
    // an integer type.
    savedSignalMask = *sigs;
#endif

    savedSignalMaskIsSet = true;
}

void saveSignalMask() {
    if (sigprocmask(SIG_BLOCK, nullptr, &savedSignalMask))
        throw SysError("querying signal mask");

    savedSignalMaskIsSet = true;
}

void startSignalHandlerThread()
{
    updateWindowSize();

    saveSignalMask();

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGWINCH);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr))
        throw SysError("blocking signals");

    std::thread(signalHandlerThread, set).detach();
}

static void restoreSignals()
{
    // If startSignalHandlerThread wasn't called, that means we're not running
    // in a proper libmain process, but a process that presumably manages its
    // own signal handlers. Such a process should call either
    //  - initNix(), to be a proper libmain process
    //  - startSignalHandlerThread(), to resemble libmain regarding signal
    //    handling only
    //  - saveSignalMask(), for processes that define their own signal handling
    //    thread
    // TODO: Warn about this? Have a default signal mask? The latter depends on
    //       whether we should generally inherit signal masks from the caller.
    //       I don't know what the larger unix ecosystem expects from us here.
    if (!savedSignalMaskIsSet)
        return;

    if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
        throw SysError("restoring signals");
}

#if __linux__
rlim_t savedStackSize = 0;
#endif

void setStackSize(size_t stackSize)
{
    #if __linux__
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur < stackSize) {
        savedStackSize = limit.rlim_cur;
        limit.rlim_cur = stackSize;
        setrlimit(RLIMIT_STACK, &limit);
    }
    #endif
}

#if __linux__
static AutoCloseFD fdSavedMountNamespace;
static AutoCloseFD fdSavedRoot;
#endif

void saveMountNamespace()
{
#if __linux__
    static std::once_flag done;
    std::call_once(done, []() {
        fdSavedMountNamespace = open("/proc/self/ns/mnt", O_RDONLY);
        if (!fdSavedMountNamespace)
            throw SysError("saving parent mount namespace");

        fdSavedRoot = open("/proc/self/root", O_RDONLY);
    });
#endif
}

void restoreMountNamespace()
{
#if __linux__
    try {
        auto savedCwd = absPath(".");

        if (fdSavedMountNamespace && setns(fdSavedMountNamespace.get(), CLONE_NEWNS) == -1)
            throw SysError("restoring parent mount namespace");

        if (fdSavedRoot) {
            if (fchdir(fdSavedRoot.get()))
                throw SysError("chdir into saved root");
            if (chroot("."))
                throw SysError("chroot into saved root");
        }

        if (chdir(savedCwd.c_str()) == -1)
            throw SysError("restoring cwd");
    } catch (Error & e) {
        debug(e.msg());
    }
#endif
}

void unshareFilesystem()
{
#ifdef __linux__
    if (unshare(CLONE_FS) != 0 && errno != EPERM)
        throw SysError("unsharing filesystem state in download thread");
#endif
}

void restoreProcessContext(bool restoreMounts)
{
    restoreSignals();
    if (restoreMounts) {
        restoreMountNamespace();
    }

    #if __linux__
    if (savedStackSize) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit) == 0) {
            limit.rlim_cur = savedStackSize;
            setrlimit(RLIMIT_STACK, &limit);
        }
    }
    #endif
}

/* RAII helper to automatically deregister a callback. */
struct InterruptCallbackImpl : InterruptCallback
{
    InterruptCallbacks::Token token;
    ~InterruptCallbackImpl() override
    {
        auto interruptCallbacks(_interruptCallbacks.lock());
        interruptCallbacks->callbacks.erase(token);
    }
};

std::unique_ptr<InterruptCallback> createInterruptCallback(std::function<void()> callback)
{
    auto interruptCallbacks(_interruptCallbacks.lock());
    auto token = interruptCallbacks->nextToken++;
    interruptCallbacks->callbacks.emplace(token, callback);

    auto res = std::make_unique<InterruptCallbackImpl>();
    res->token = token;

    return std::unique_ptr<InterruptCallback>(res.release());
}


AutoCloseFD createUnixDomainSocket()
{
    AutoCloseFD fdSocket = socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0);
    if (!fdSocket)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(fdSocket.get());
    return fdSocket;
}


AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(fdSocket.get(), path);

    if (chmod(path.c_str(), mode) == -1)
        throw SysError("changing permissions on '%1%'", path);

    if (listen(fdSocket.get(), 100) == -1)
        throw SysError("cannot listen on socket '%1%'", path);

    return fdSocket;
}


void bind(int fd, const std::string & path)
{
    unlink(path.c_str());

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pid pid = startProcess([&]() {
            Path dir = dirOf(path);
            if (chdir(dir.c_str()) == -1)
                throw SysError("chdir to '%s' failed", dir);
            std::string base(baseNameOf(path));
            if (base.size() + 1 >= sizeof(addr.sun_path))
                throw Error("socket path '%s' is too long", base);
            memcpy(addr.sun_path, base.c_str(), base.size() + 1);
            if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                throw SysError("cannot bind to socket '%s'", path);
            _exit(0);
        });
        int status = pid.wait();
        if (status != 0)
            throw Error("cannot bind to socket '%s'", path);
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
            throw SysError("cannot bind to socket '%s'", path);
    }
}


void connect(int fd, const std::string & path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pid pid = startProcess([&]() {
            Path dir = dirOf(path);
            if (chdir(dir.c_str()) == -1)
                throw SysError("chdir to '%s' failed", dir);
            std::string base(baseNameOf(path));
            if (base.size() + 1 >= sizeof(addr.sun_path))
                throw Error("socket path '%s' is too long", base);
            memcpy(addr.sun_path, base.c_str(), base.size() + 1);
            if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                throw SysError("cannot connect to socket at '%s'", path);
            _exit(0);
        });
        int status = pid.wait();
        if (status != 0)
            throw Error("cannot connect to socket at '%s'", path);
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
            throw SysError("cannot connect to socket at '%s'", path);
    }
}


std::string showBytes(uint64_t bytes)
{
    return fmt("%.2f MiB", bytes / (1024.0 * 1024.0));
}


// FIXME: move to libstore/build
void commonChildInit()
{
    logger = makeSimpleLogger();

    const static std::string pathNullDevice = "/dev/null";
    restoreProcessContext(false);

    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError("creating a new session");

    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError("cannot open '%1%'", pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
    close(fdDevNull);
}

}
