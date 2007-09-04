#include "config.h"

#include "util.hh"

#ifdef __CYGWIN__
#include <windows.h>
#endif

#include <iostream>
#include <cerrno>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

extern char * * environ;


namespace nix {


Error::Error(const format & f)
{
    err = f.str();
}


Error & Error::addPrefix(const format & f)
{
    err = f.str() + err;
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
                followCount++;
                if (followCount >= maxFollow)
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


unsigned long long computePathSize(const Path & path)
{
    unsigned long long size = 0;
    
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    size += st.st_size;

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            size += computePathSize(path + "/" + *i);
    }

    return size;
}


static void _deletePath(const Path & path, unsigned long long & bytesFreed)
{
    checkInterrupt();

    printMsg(lvlVomit, format("%1%") % path);

    struct stat st;
    if (lstat(path.c_str(), &st))
		throw SysError(format("getting attributes of path `%1%'") % path);

    bytesFreed += st.st_size;

    if (S_ISDIR(st.st_mode)) {
	Strings names = readDirectory(path);

	/* Make the directory writable. */
	if (!(st.st_mode & S_IWUSR)) {
	    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
			throw SysError(format("making `%1%' writable") % path);
	}

	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            _deletePath(path + "/" + *i, bytesFreed);
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
}


void deletePath(const Path & path)
{
    unsigned long long dummy;
    deletePath(path, dummy);
}


void deletePath(const Path & path, unsigned long long & bytesFreed)
{
    startNest(nest, lvlDebug,
        format("recursively deleting path `%1%'") % path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed);
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


static Path tempName(const Path & tmpRoot)
{
    static int counter = 0;
    Path tmpRoot2 = canonPath(tmpRoot.empty() ? getEnv("TMPDIR", "/tmp") : tmpRoot, true);
    return (format("%1%/nix-%2%-%3%") % tmpRoot2 % getpid() % counter++).str();
}


Path createTempDir(const Path & tmpRoot)
{
    while (1) {
        checkInterrupt();
	Path tmpDir = tempName(tmpRoot);
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

void createDirs(const Path & path)
{
    if (path == "/") return;
    createDirs(dirOf(path));
    if (!pathExists(path))
        if (mkdir(path.c_str(), 0777) == -1)
            throw SysError(format("creating directory `%1%'") % path);
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
//Verbosity verbosity = lvlDebug;
//Verbosity verbosity = lvlVomit;

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
        if (res == 0) 
        	throw EndOfFile("unexpected end-of-file (in readFull so deamon communication)");
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


AutoCloseFD::AutoCloseFD(const AutoCloseFD & fd)
{
    abort();
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
            std::cerr << format("killing processes beloging to uid `%1%': %1%\n")
                % uid % e.what();
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
                throw SysError("dupping from-hook write side");

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

/*
string packRevisionNumbers(const UnsignedIntVector & revs)
{
    string seperator = "|";
    string d = "";
    for (UnsignedIntVector::const_iterator i = revs.begin(); i != revs.end(); ++i){
        d += unsignedInt2String(*i) + seperator;
    }
    return d;
}
    
UnsignedIntVector unpackRevisionNumbers(const string & packed)
{
    string seperator = "|";
    Strings ss = tokenizeString(packed, seperator);
    UnsignedIntVector revs;
    
    for (Strings::const_iterator i = ss.begin(); i != ss.end(); ++i){
    	int rev;
    	bool succeed = string2UnsignedInt(*i, rev);
    	if(!succeed)
    		throw Error(format("Corrupted revisions db entry: `%1%'") % packed);
    	revs.push_back(rev);
    }
        
    return revs;
}
*/

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

//Splits on a space
//Uses quotes: "...." to group multiple spaced arguments into one
//Removes the grouping quotes at the call 
//TODO bug a call with args: a b "c" d   will fail
//TODO bug a call with args: a bc"d " e  will fail
Strings tokenizeStringWithQuotes(const string & s)
{
	string separator = " ";
    Strings result;
    string::size_type pos = s.find_first_not_of(separator, 0);

	string quote = "\"";
	string inquotes = "";

    while (pos != string::npos) {
        string::size_type end = s.find_first_of(separator, pos + 1);
        if (end == string::npos) end = s.size();
        string token(s, pos, end - pos);
        
        pos = s.find_first_not_of(separator, end);
        
        //if first or last item is a quote, we switch inquotes
        if(token.substr(0,1) == quote || token.substr(token.size()-1,token.size()) == quote){
        	if(inquotes == ""){			//begin
        		inquotes = token;
        		continue;
        	} 
        	else{						//end
        		token = inquotes + token;
        		token = token.substr(1,token.size()-2);		//remove the grouping quotes
        		inquotes = "";
        	}     		
        }
        
       	if(inquotes != "")				//middle
			inquotes += separator + token;
        else
        	result.push_back(token);	//normal
    }
    return result;
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


string int2String(int n)
{
    std::ostringstream str;
    str << n;
    return str.str();
}


bool string2Int(const string & s, int & n)
{
    std::istringstream str(s);
    str >> n;
    return str && str.get() == EOF;
}

string unsignedInt2String(unsigned int n)
{
    std::ostringstream str;
    str << n;
    return str.str();
}

bool string2UnsignedInt(const string & s, unsigned int & n)
{
    std::istringstream str(s);
    str >> n;
    return str && str.get() == EOF;
}

string bool2string(const bool b)
{
	if(b == true)
	  return "true";
	else
      return "false";
}

bool string2bool(const string & s)
{
	if(s == "true")
	  return true;
	else if(s == "false")
      return false;
    else{
    	throw Error(format("cannot convert string: `%1%' to bool") % s);
        quickExit(1);
    	return false;
    }
}

string triml(const string & s) {
	string news = s;
	int pos(0);
	for ( ; news[pos]==' ' || news[pos]=='\t'; ++pos );
		news.erase(0, pos);
	return news;
}

string trimr(const string & s) {
	string news = s;	
	int pos(news.size());
	for ( ; pos && news[pos-1]==' ' || news[pos]=='\t'; --pos );
		news.erase(pos, news.size()-pos);
	return news;
}

string trim(const string & s) {
	return triml(trimr(s));
}


//executes a shell command, captures and prints the output.
void runProgram_AndPrintOutput(Path program, bool searchPath, const Strings & args, const string outputPrefix)
{
	string program_output = runProgram(program, searchPath, args);

	//Add the prefix on every line
	Strings lines = tokenizeString(program_output, "\n");
	for (Strings::const_iterator i = lines.begin(); i != lines.end(); ++i){
		if(trim(*i) != "")
			printMsg(lvlError, format("[%2%]: %1%") % *i % outputPrefix);
	}
}

//executes a direct shell command (faster)
void executeShellCommand(const string & command)
{
	int kidstatus, deadpid;
	pid_t kidpid = fork();
    switch (kidpid) {
	    case -1:
	        throw SysError("unable to fork");
	    case 0:
	        try { // child
	            int rv = system(command.c_str());
				//int rv = execlp(svnbin.c_str(), svnbin.c_str(), ">", tempoutput.c_str(), NULL);		//TODO make this work ... ?
				
				if (rv == -1) {
					throw SysError("executeShellCommand(..) error");
					quickExit(99);
				}
		        quickExit(0);
				
	        } catch (std::exception & e) {
	            std::cerr << format("executeShellCommand(..) child error: %1%\n") % e.what();
	            quickExit(1);
	        }
    }
    
    deadpid = waitpid(kidpid, &kidstatus, 0);
    if (deadpid == -1) {
        std::cerr << format("state child waitpid error\n");
        quickExit(1);
    }
}

unsigned int getTimeStamp()
{
	const time_t now = time(0);
	unsigned int i = now;
	return i;
}

//TODO Does this work on windows?
bool FileExist(const string FileName)
{
	const char* FileName_C = FileName.c_str();
    struct stat my_stat;
    if (stat(FileName_C, &my_stat) != 0) return false;
    return ((my_stat.st_mode & S_IFREG) != 0);
    
    /*
       S_IFMT     0170000   bitmask for the file type bitfields
       S_IFSOCK   0140000   socket
       S_IFLNK    0120000   symbolic link
       S_IFREG    0100000   regular file
       S_IFBLK    0060000   block device
       S_IFDIR    0040000   directory
       S_IFCHR    0020000   character device
       S_IFIFO    0010000   fifo
    */
}

//TODO Does this work on windows?
bool DirectoryExist(const string FileName)
{
	const char* FileName_C = FileName.c_str();
    struct stat my_stat;
    if (stat(FileName_C, &my_stat) != 0) return false;
    return ((my_stat.st_mode & S_IFDIR) != 0);
}

//TODO Does this work on windows?

bool IsSymlink(const string FileName)
{
	const char* FileName_C = FileName.c_str();
    struct stat my_stat;
    if (lstat(FileName_C, &my_stat) != 0) return false;
    return (S_ISLNK(my_stat.st_mode) != 0);
}


/*
string getCallingUserName()
{
    //Linux
    Strings empty;
    string username = runProgram("whoami", true, empty);				//the username of the user that is trying to build the component
																		//TODO Can be faked, so this is clearly unsafe ... :(
    //Remove the trailing \n
    int pos = username.find("\n",0);
	username.erase(pos,1);
    
    return username;
}
*/

//merges two PathSets into one, removing doubles (union)
PathSet pathSets_union(const PathSet & paths1, const PathSet & paths2)
{
    vector<Path> vector1(paths1.begin(), paths1.end());
  	vector<Path> vector2(paths2.begin(), paths2.end());
  	vector<Path> setResult;
  	
  	set_union(vector1.begin(), vector1.end(),vector2.begin(), vector2.end(), back_inserter(setResult)); 	//Also available: set_symmetric_difference and set_intersection

	PathSet diff;	
	for(unsigned int i=0; i<setResult.size(); i++)
		diff.insert(setResult[i]);

    return diff;
}

void pathSets_difference(const PathSet & oldpaths, const PathSet & newpaths, PathSet & addedpaths, PathSet & removedpaths)
{
	for (PathSet::iterator i = oldpaths.begin(); i != oldpaths.end(); ++i){
		bool exists = false;
		for (PathSet::iterator j = newpaths.begin(); j != newpaths.end(); ++j){
			if(*i == *j){
				exists = true;
				break;
			}
		}
		if(!exists)
			removedpaths.insert(*i);			
	}
	
	for (PathSet::iterator i = newpaths.begin(); i != newpaths.end(); ++i){
		bool exists = false;
		for (PathSet::iterator j = oldpaths.begin(); j != oldpaths.end(); ++j){
			if(*i == *j){
				exists = true;
				break;
			}
		}
		if(!exists)
			addedpaths.insert(*i);			
	}
}

void ensureDirExists(const Path & path)
{
	Strings p_args;
	p_args.push_back("-p");
	p_args.push_back(path);
	runProgram_AndPrintOutput("mkdir", true, p_args, "mkdir");		//TODO ensurePath
}

void setChown(const Path & pathOrFile, const string & user, const string & group, bool recursive)
{
	Strings p_args;
	if(recursive)
		p_args.push_back("-R");
	p_args.push_back(user + "." + group);
	p_args.push_back(pathOrFile);
	runProgram_AndPrintOutput("chown", true, p_args, "chown");
} 

void setChmod(const Path & pathOrFile, const string & chmod)
{
	Strings p_args;
	p_args.push_back(chmod);
	p_args.push_back(pathOrFile);
	runProgram_AndPrintOutput("chmod", true, p_args, "chmod");
}

string padd(const string & s, char c , unsigned int size, bool front)
{
	string ss = s;
	while (ss.length() < size){
		if(front)
			ss = c + ss;
		else
			ss = ss  + c;
	}
	return ss;
}

void symlinkPath(const Path & fromExisting, const Path & toNew)
{
	//Symlink link to the share path
	//Usage: ln [OPTION]... [-T] TARGET LINK_NAME   (1st form)
	Strings p_args;
	p_args.push_back("-sf");
	p_args.push_back(fromExisting);	
	p_args.push_back(toNew);
	runProgram_AndPrintOutput("ln", true, p_args, "ln");
	//printMsg(lvlError, format("ln -sf %1% %2%") % fromExisting % toNew);
}

void copyContents(const Path & from, const Path & to)
{
	Strings p_args;
	p_args.push_back("-R");
	p_args.push_back(from + "/");		//the / makes sure it copys the contents of the dir, not just the symlink
	p_args.push_back(to);
	runProgram_AndPrintOutput("cp", true, p_args, "cp");
}

}
