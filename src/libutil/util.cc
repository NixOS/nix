#include "config.h"

#include <iostream>
#include <cerrno>
#include <cstdio>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#include "util.hh"


string thisSystem = SYSTEM;


Error::Error(const format & f)
{
    err = f.str();
}


SysError::SysError(const format & f)
    : Error(format("%1%: %2%") % f.str() % strerror(errno))
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


Path canonPath(const Path & path)
{
    string s;

    if (path[0] != '/')
        throw Error(format("not an absolute path: `%1%'") % path);

    string::const_iterator i = path.begin(), end = path.end();

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
        }
    }

    return s.empty() ? "/" : s;
}


Path dirOf(const Path & path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name: %1%") % path);
    return Path(path, 0, pos);
}


string baseNameOf(const Path & path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name %1% ") % path);
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


static void _deletePath(const Path & path)
{
    checkInterrupt();

    printMsg(lvlVomit, format("%1%") % path);

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	/* Make the directory writable. */
	if (!(st.st_mode & S_IWUSR)) {
	    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
		throw SysError(format("making `%1%' writable") % path);
	}

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            _deletePath(path + "/" + *i);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
}


void deletePath(const Path & path)
{
    startNest(nest, lvlDebug,
        format("recursively deleting path `%1%'") % path);
    _deletePath(path);
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


static Path tempName()
{
    static int counter = 0;
    Path tmpRoot = canonPath(getEnv("TMPDIR", "/tmp"));
    return (format("%1%/nix-%2%-%3%") % tmpRoot % getpid() % counter++).str();
}


Path createTempDir()
{
    while (1) {
        checkInterrupt();
	Path tmpDir = tempName();
	if (mkdir(tmpDir.c_str(), 0777) == 0) return tmpDir;
	if (errno != EEXIST)
	    throw SysError(format("creating directory `%1%'") % tmpDir);
    }
}


void writeStringToFile(const Path & path, const string & s)
{
    AutoCloseFD fd = open(path.c_str(),
        O_CREAT | O_EXCL | O_WRONLY, 0666);
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
    int l = (int) level;
    ostringstream st;
    st << l;
    return st.str();
}


void Nest::open(Verbosity level, const format & f)
{
    if (level <= verbosity) {
        if (logType == ltEscapes)
            cerr << "\033[" << escVerbosity(level) << "p"
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
            cerr << "\033[q";
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
    cerr << format("%1%%2%\n") % prefix % f.str();
}


void readFull(int fd, unsigned char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, (char *) buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw SysError("reading from file");
        }
        if (res == 0) throw Error("unexpected end-of-file");
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



//////////////////////////////////////////////////////////////////////


AutoDelete::AutoDelete(const string & p) : path(p)
{
    del = true;
}

AutoDelete::~AutoDelete()
{
    if (del) deletePath(path);
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


AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (Error & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.msg());
    }
}


void AutoCloseFD::operator =(int fd)
{
    if (this->fd != fd) close();
    this->fd = fd;
}


AutoCloseFD::operator int()
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
}


Pid::~Pid()
{
    kill();
}


void Pid::operator =(pid_t pid)
{
    if (this->pid != pid) kill();
    this->pid = pid;
}


Pid::operator pid_t()
{
    return pid;
}


void Pid::kill()
{
    if (pid == -1) return;
    
    printMsg(lvlError, format("killing process %1%") % pid);

    /* Send a KILL signal to the child.  If it has its own process
       group, send the signal to every process in the child process
       group (which hopefully includes *all* its children). */
    if (::kill(separatePG ? -pid : pid, SIGKILL) != 0)
        printMsg(lvlError, (SysError(format("killing process %1%") % pid).msg()));

    /* Wait until the child dies, disregarding the exit status. */
    int status;
    while (waitpid(pid, &status, 0) == -1)
        if (errno != EINTR) printMsg(lvlError,
            (SysError(format("waiting for process %1%") % pid).msg()));

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
    }
}


void Pid::setSeparatePG(bool separatePG)
{
    this->separatePG = separatePG;
}



//////////////////////////////////////////////////////////////////////


volatile sig_atomic_t _isInterrupted = 0;

void _interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!uncaught_exception()) {
        _isInterrupted = 0;
        throw Error("interrupted by the user");
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


string statusToString(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            return (format("failed with exit code %1%") % WEXITSTATUS(status)).str();
        else if (WIFSIGNALED(status))
            return (format("failed due to signal %1%") % WTERMSIG(status)).str();
        else
            return "died abnormally";
    } else return "succeeded";
}


bool statusOk(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


bool string2Int(const string & s, int & n)
{
    istringstream str(s);
    str >> n;
    return str && str.eof();
}
