#include <iostream>
#include <cerrno>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

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


string absPath(string path, string dir)
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


string canonPath(const string & path)
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


string dirOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name: %1%") % path);
    return string(path, 0, pos);
}


string baseNameOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name %1% ") % path);
    return string(path, pos + 1);
}


bool pathExists(const string & path)
{
    int res;
    struct stat st;
    res = stat(path.c_str(), &st);
    if (!res) return true;
    if (errno != ENOENT)
        throw SysError(format("getting status of %1%") % path);
    return false;
}


void deletePath(const string & path)
{
    msg(lvlVomit, format("deleting path `%1%'") % path);

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
	Strings names;

        DIR * dir = opendir(path.c_str());

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir)) {
            string name = dirent->d_name;
            if (name == "." || name == "..") continue;
	    names.push_back(name);
        }

        closedir(dir); /* !!! close on exception */

	/* Make the directory writable. */
	if (!(st.st_mode & S_IWUSR)) {
	    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
		throw SysError(format("making `%1%' writable"));
	}

	for (Strings::iterator i = names.begin(); i != names.end(); i++)
            deletePath(path + "/" + *i);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
}


void makePathReadOnly(const string & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (!S_ISLNK(st.st_mode) && (st.st_mode & S_IWUSR)) {
	if (chmod(path.c_str(), st.st_mode & ~S_IWUSR) == -1)
	    throw SysError(format("making `%1%' read-only") % path);
    }

    if (S_ISDIR(st.st_mode)) {
        DIR * dir = opendir(path.c_str());

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir)) {
            string name = dirent->d_name;
            if (name == "." || name == "..") continue;
	    makePathReadOnly(path + "/" + name);
        }

        closedir(dir); /* !!! close on exception */
    }
}


static string tempName()
{
    static int counter = 0;
    char * s = getenv("TMPDIR");
    string tmpRoot = s ? canonPath(string(s)) : "/tmp";
    return (format("%1%/nix-%2%-%3%") % tmpRoot % getpid() % counter++).str();
}


string createTempDir()
{
    while (1) {
	string tmpDir = tempName();
	if (mkdir(tmpDir.c_str(), 0777) == 0) return tmpDir;
	if (errno != EEXIST)
	    throw SysError(format("creating directory `%1%'") % tmpDir);
    }
}


Verbosity verbosity = lvlError;

static int nestingLevel = 0;


Nest::Nest(Verbosity level, const format & f)
{
    if (level > verbosity)
        nest = false;
    else {
        msg(level, f);
        nest = true;
        nestingLevel++;
    }
}


Nest::~Nest()
{
    if (nest) nestingLevel--;
}


void msg(Verbosity level, const format & f)
{
    if (level > verbosity) return;
    string spaces;
    for (int i = 0; i < nestingLevel; i++)
        spaces += "|   ";
    cerr << format("%1%%2%\n") % spaces % f.str();
}


void debug(const format & f)
{
    msg(lvlDebug, f);
}


void readFull(int fd, unsigned char * buf, size_t count)
{
    while (count) {
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
        ssize_t res = write(fd, (char *) buf, count);
        if (res == -1) throw SysError("writing to file");
        count -= res;
        buf += res;
    }
}
