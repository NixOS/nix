#include <iostream>
#include <cerrno>
#include <cstdio>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

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
    if (errno != ENOENT)
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


void deletePath(const Path & path)
{
    checkInterrupt();

    printMsg(lvlVomit, format("deleting path `%1%'") % path);

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	/* Make the directory writable. */
	if (!(st.st_mode & S_IWUSR)) {
	    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
		throw SysError(format("making `%1%' writable"));
	}

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            deletePath(path + "/" + *i);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
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
    char * s = getenv("TMPDIR");
    Path tmpRoot = s ? canonPath(Path(s)) : "/tmp";
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
Verbosity verbosity = lvlError;

static int nestingLevel = 0;


Nest::Nest()
{
    nest = false;
}


Nest::~Nest()
{
    if (nest) {
        nestingLevel--;
        if (logType == ltEscapes)
            cerr << "\033[q";
    }
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
            cerr << "\033[" << escVerbosity(level) << "p";
        printMsg_(level, f);
        nest = true;
        nestingLevel++;
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
        if (res == -1) throw SysError("reading from file");
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
        if (res == -1) throw SysError("writing to file");
        count -= res;
        buf += res;
    }
}


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
    if (fd != -1) close(fd);
}

void AutoCloseFD::operator =(int fd)
{
    this->fd = fd;
}

AutoCloseFD::operator int()
{
    return fd;
}


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


volatile sig_atomic_t _isInterrupted = 0;

void _interrupted()
{
    _isInterrupted = 0;
    throw Error("interrupted by the user");
}
