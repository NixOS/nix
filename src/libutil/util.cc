#include "lazy.hh"
#include "util.hh"
#include "affinity.hh"
#include "sync.hh"
#include "finally.hh"
#include "serialise.hh"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include <future>

#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <sys/syscall.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifdef _WIN32
#define BOOST_STACKTRACE_USE_BACKTRACE
#include <boost/stacktrace.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <codecvt>
#define random() rand()
#else
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

extern char * * environ;
#endif


namespace nix {

#ifdef _WIN32
std::string to_bytes(const std::wstring & path) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(path);
}
std::wstring from_bytes(const std::string & s) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(s);
}

std::optional<std::wstring> maybePathW(const Path & path) {
    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z')) && path[1] == ':' && isslash(path[2])) {
        std::wstring sw = from_bytes("\\\\?\\" + path);
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?') && path[3] == '\\' &&
               ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':' && isslash(path[6])) {
        std::wstring sw = from_bytes(path);
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    return std::optional<std::wstring>();
}

std::wstring pathW(const Path & path) {
    std::optional<std::wstring> sw = maybePathW(path);
    if (!sw) {
        std::cerr << "invalid path for WinAPI call ["<<path<<"]"<<std::endl;
        _exit(111);
    }
    return *sw;
}


std::wstring handleToFileName(HANDLE handle) {
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED);
    if (dw == 0) {
        if (handle == GetStdHandle(STD_INPUT_HANDLE )) return L"<stdin>";
        if (handle == GetStdHandle(STD_OUTPUT_HANDLE)) return L"<stdout>";
        if (handle == GetStdHandle(STD_ERROR_HANDLE )) return L"<stderr>";
        return (boost::wformat(L"<unnnamed handle %X>") % handle).str();
    }
    if (dw > buf.size()) {
        buf.resize(dw);
        if (GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED) != dw-1)
            throw WinError("GetFinalPathNameByHandleW");
        dw -= 1;
    }
    return std::wstring(buf.data(), dw);
}

Path handleToPath(HANDLE handle) {
    return to_bytes(handleToFileName(handle));
}

#endif

static inline Path::size_type rfindSlash(const Path & path, Path::size_type from = Path::npos) {
#ifdef _WIN32
    Path::size_type p1 = path.rfind('/', from);
    Path::size_type p2 = path.rfind('\\', from);
    return p1 == Path::npos ? p2 :
           p2 == Path::npos ? p1 :
           std::max(p1, p2);
#else
    return path.rfind('/', from);
#endif
}

BaseError & BaseError::addPrefix(const FormatOrString & fs)
{
    prefix_ = fs.s + prefix_;
    return *this;
}


std::string PosixError::addErrno(const std::string & s)
{
    errNo = errno;
    return s + ": " + strerror(errNo);
}

#ifdef _WIN32
std::string WinError::addLastError(const std::string & s)
{
    lastError = GetLastError();
    LPSTR errorText = NULL;

    FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM // use system message tables to retrieve error text
                   |FORMAT_MESSAGE_ALLOCATE_BUFFER // allocate buffer on local heap for error text
                   |FORMAT_MESSAGE_IGNORE_INSERTS,   // Important! will fail otherwise, since we're not  (and CANNOT) pass insertion parameters
                    NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
                    lastError,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&errorText,  // output
                    0, // minimum size for output buffer
                    NULL);   // arguments - see note

    if (NULL != errorText ) {
        std::string s2 = (format("%1% lastError=%2% %3%") % s % lastError % errorText).str();
        LocalFree(errorText);
        return s2;
    }
    return (format("%1% lastError=%2%") % s % lastError).str();
}
#endif

#ifdef _WIN32
std::wstring getCwdW()
{
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetCurrentDirectoryW(buf.size(), buf.data());
    if (dw == 0) {
        throw WinError("GetCurrentDirectoryW");
    }
    if (dw > buf.size()) {
        buf.resize(dw);
        if (GetCurrentDirectoryW(buf.size(), buf.data()) != dw-1)
            throw WinError("GetCurrentDirectoryW");
        dw -= 1;
    }
    return std::wstring(buf.data(), dw);
}

std::wstring getArgv0W()
{
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetModuleFileNameW(NULL, buf.data(), buf.size());
    if (dw == 0) {
        throw WinError("GetModuleFileNameW");
    }
    while (dw >= buf.size()) {
        buf.resize(dw*2);
        DWORD dw2 = GetModuleFileNameW(NULL, buf.data(), buf.size());
        if (dw2 == dw) break;
        assert(dw2 > dw);
        dw = dw2;
    }
    return std::wstring(buf.data(), dw);
}

std::wstring getEnvW(const std::wstring & key, const std::wstring & def)
{
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetEnvironmentVariableW(key.c_str(), buf.data(), buf.size());
    if (0 == dw) {
        WinError winError("GetEnvironmentVariableW '%1%'", to_bytes(key));
        if (winError.lastError != ERROR_ENVVAR_NOT_FOUND)
            throw winError;
        return def;
    }
    if (dw > buf.size()) {
        buf.resize(dw);
        if (GetEnvironmentVariableW(key.c_str(), buf.data(), buf.size()) != dw-1)
            throw WinError("GetEnvironmentVariableW");
        dw -= 1;
    }
    return std::wstring(buf.data(), dw);
}

string getEnv(const string & key, const string & def)
{
    return to_bytes(getEnvW(from_bytes(key), from_bytes(def)));
}

std::map<std::wstring, std::wstring, no_case_compare> getEntireEnvW()
{
    std::map<std::wstring, std::wstring, no_case_compare> env;
    const wchar_t * pw = GetEnvironmentStringsW();
    while (*pw) {
        size_t len = wcslen(pw);
        const wchar_t * peq = wcschr(pw+1, L'=');
        if (peq < pw + len) {
            env[std::wstring(pw, peq-pw)] = std::wstring(peq+1, pw+len-(peq+1));
        } else {
            env[std::wstring(pw, len)] = L"";
        }
        pw += len + 1;
    }
    return env;
}

#else
string getEnv(const string & key, const string & def)
{
    char * value = getenv(key.c_str());
    return value ? string(value) : def;
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
#endif



#ifndef _WIN32
Path absPath(Path path, Path dir)
{
//  std::cerr << "absPath("<<path<<", "<<dir<<")"<<std::endl;
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
                throw PosixError("cannot get cwd");
            dir = buf;
#ifdef __GNU__
            free(buf);
#endif
        }
        path = dir + "/" + path;
    }
    return canonPath(path);
}
#else
Path absPath(Path path, Path dir)
{
//  std::cerr << "absPath("<<path<<", "<<dir<<")"<<std::endl;

    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z')) && path[1] == ':' && isslash(path[2])) {
        return canonPath(path);
    }
    if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?') && path[3] == '\\' &&
               ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':' && isslash(path[6])) {
        return canonPath(path);
    }
    if (path.length() >= 1 && isslash(path[0]))
        throw Error("cannot absolutize posix path '%1%'", path); // todo: use cygpath?

    if (dir.empty())
        dir = to_bytes(getCwdW());
    return canonPath(dir + "/" + path);
}
#endif


std::optional<Path> maybeCanonPath(const Path & path, bool resolveSymlinks)
{
//std::cerr << "maybeCanonPath("<<path<<", resolveSymlinks="<<resolveSymlinks<<")"<<std::endl;

// if (path.find(';') != Path::npos){
//     std::cerr << boost::stacktrace::stacktrace() << std::endl;
// }
    assert(!path.empty());

    Path::const_iterator i = path.begin(), end = path.end();
    Path s;
#ifdef _WIN32
    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z')) && path[1] == ':' && isslash(path[2])) {
        i += 2;
        s = /*Path("\\\\.\\") +*/ Path() + char(path[0] & ~0x20) + ":";
    } else if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?') && path[3] == '\\' &&
               ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':' && isslash(path[6])) {
        i += 6;
        s = path.substr(4, 2);
    } else
        return std::optional<Path>();
#else
    if (path[0] != '/')
        return std::optional<Path>();
#endif

    string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    while (1) {

        /* Skip slashes. */
        while (i != end && isslash(*i)) i++;
        if (i == end) break;

        /* Ignore `.'. */
        if (*i == '.' && (i + 1 == end || isslash(i[1])))
            i++;

        /* If `..', delete the last component. */
        else if (*i == '.' && i + 1 < end && i[1] == '.' &&
            (i + 2 == end || isslash(i[2])))
        {
            if (!s.empty()) s.erase(rfindSlash(s));
            i += 2;
        }

        /* Normal component; copy it. */
        else {
            s += '/';
            while (i != end && !isslash(*i)) s += *i++;

            /* If s points to a symlink, resolve it and restart (since
               the symlink target might contain new symlinks). */
            if (resolveSymlinks && isLink(s)) {
                if (++followCount >= maxFollow)
                    throw Error(format("infinite symlink recursion in path '%1%'") % path);
                temp = absPath(readLink(s), dirOf(s))
                    + string(i, end);
                end = temp.end();
#ifdef _WIN32
                i = temp.begin() + 2;
                s = temp.substr(0, 2);
#else
                i = temp.begin(); /* restart */
                s = "";
#endif
            }
        }
    }
#ifndef _WIN32
    s = s.empty() ? "/" : s;
#else
//    std::cerr << "canonPath("<<path<<", resolveSymlinks="<<resolveSymlinks<<") -> ["<<s<<"]"<<std::endl;
#endif
    return s;
}

Path canonPath(const Path & path, bool resolveSymlinks) {
    std::optional<Path> mb = maybeCanonPath(path, resolveSymlinks);
    if (!mb)
        throw Error(format("not an absolute path: '%1%'") % path);
    return *mb;
}

/* for paths inside .nar */
Path canonNarPath(const Path & path)
{
    assert(!path.empty());

    Path::const_iterator i = path.begin(), end = path.end();
    Path s;
    if (path[0] != '/')
        throw Error("not an absolute nar path '%1%'", path);

    while (1) {
        /* Skip slashes. */
        while (i != end && isslash(*i)) i++;
        if (i == end) break;

        /* Ignore `.'. */
        if (*i == '.' && (i + 1 == end || isslash(i[1])))
            i++;

        /* If `..', delete the last component. */
        else if (*i == '.' && i + 1 < end && i[1] == '.' &&
            (i + 2 == end || isslash(i[2])))
        {
            if (!s.empty()) s.erase(rfindSlash(s));
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
    Path::size_type pos = rfindSlash(path);
//    Path::size_type pos = path.rfind('/');
    if (pos == string::npos)
        return ".";
// fprintf(stderr, "dirOf(%s)\n", path.c_str());
#ifdef _WIN32
    if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?') && path[3] == '\\' &&
               ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':' && isslash(path[6])) {
//        Path::size_type pos = rfindSlash(path);
        if (pos == string::npos || pos < 6)
            throw Error(format("invalid file name1 '%1%'") % path);
        Path rc = Path(path, 0, pos == 6 ? 7 : pos);
// fprintf(stderr, "dirOf1(%s) -> [%s]%d\n", path.c_str(), rc.c_str(), path == rc);
        return rc;
    }
    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z')) && path[1] == ':' && isslash(path[2])) {
//        Path::size_type pos = rfindSlash(path);
        if (pos == string::npos || pos < 2)
            throw Error(format("invalid file name2 '%1%'") % path);
        Path rc = Path(path, 0, pos == 2 ? 3 : pos);
// fprintf(stderr, "dirOf2(%s) -> [%s]%d\n", path.c_str(), rc.c_str(), path == rc);
        return rc;
    }
    if (pos == 0)
        throw Error(format("invalid file name9 '%1%'") % path);
    return Path(path, 0, pos);
#else
    Path rc = pos == 0 ? "/" : Path(path, 0, pos);
// fprintf(stderr, "dirOf3(%s) -> [%s]%d\n", path.c_str(), rc.c_str(), path == rc);
    return rc;
#endif
}


string baseNameOf(const Path & path)
{
// fprintf(stderr, "baseNameOf(%s)\n", path.c_str());
    if (path.empty())
        return "";

    Path::size_type last = path.length() - 1;
    if (isslash(path[last]) && last > 0)
        last -= 1;

    Path::size_type pos = rfindSlash(path, last);
    if (pos == string::npos)
        pos = 0;
    else
        pos += 1;

    Path rc = string(path, pos, last - pos + 1);
// fprintf(stderr, "baseNameOf(%s) -> [%s]\n", path.c_str(), rc.c_str());
    return rc;
}


bool isInDir(const Path & path, const Path & dir)
{
//std::cerr << "isInDir("<<path<<", "<<dir<<")" << std::endl;
#ifndef _WIN32
    return path[0] == '/'
        && path.compare(0, dir.size(), dir) == 0
        && path.size() >= dir.size() + 2
        && path[dir.size()] == '/';
#else
    std::optional<Path> cpath = maybeCanonPath(path, false);
    if (!cpath)
        return false;
    std::optional<Path> cdir  = maybeCanonPath(dir, false);
    if (!cdir)
        return false;
    return (*cpath).compare(0, (*cdir).size(), *cdir) == 0
        && (*cpath).size() >= (*cdir).size() + 2
        && (*cpath)[(*cdir).size()] == '/';
#endif
}


bool isDirOrInDir(const Path & path, const Path & dir)
{
    return path == dir || isInDir(path, dir);
}

// todo: get rid of stat(), is has a lot of unexpected MSYS magic
struct stat lstatPath(const Path & path)
{
    struct stat st;
#ifndef _WIN32
    if (lstat(path.c_str(), &st))
#else
    if (stat(path.c_str(), &st))
#endif
        throw PosixError(format("getting status-1 of '%1%'") % path);
    return st;
}


#ifndef _WIN32
bool pathExists(const Path & path)
{
    int res;
    struct stat st;
    res = lstat(path.c_str(), &st);
    if (!res) {
        return true;
    }
    if (errno != ENOENT && errno != ENOTDIR) {
        throw PosixError(format("getting status-2 of %1%") % path);
    }
    return false;
}
#else
bool pathExists(const Path & path) {
//    std::cerr << "pathExists(" << path << ")" << std::endl;
//    std::cerr << "pathExists(absPath=" << absPath(path) << ")" << std::endl;
    DWORD dw = GetFileAttributesW(pathW(absPath(path)).c_str()); // absPath is to remove "//"
//    std::cerr << "pathExists(" << path << ") dw=" << dw << std::endl;
    if (dw != 0xFFFFFFFF)
        return true;
    WinError winError("GetFileAttributesW when pathExists %1%", path);
    if (winError.lastError == ERROR_FILE_NOT_FOUND || winError.lastError == ERROR_PATH_NOT_FOUND)
        return false;
    throw winError;
}

#endif


#ifdef _WIN32
// from Windows SDK, mingw headers miss it
typedef struct _REPARSE_DATA_BUFFER
{
    ULONG   ReparseTag;
    USHORT  ReparseDataLength;
    USHORT  Reserved;

    union
    {
        struct
        {
            USHORT  SubstituteNameOffset;
            USHORT  SubstituteNameLength;
            USHORT  PrintNameOffset;
            USHORT  PrintNameLength;
            ULONG   Flags;
            WCHAR   PathBuffer[1];
        } SymbolicLinkReparseBuffer;

        struct
        {
            USHORT  SubstituteNameOffset;
            USHORT  SubstituteNameLength;
            USHORT  PrintNameOffset;
            USHORT  PrintNameLength;
            WCHAR   PathBuffer[1];
        } MountPointReparseBuffer;

        struct
        {
            UCHAR   DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

Path readLink(const Path & path)
{
    checkInterrupt();
#ifndef _WIN32
    std::vector<char> buf;
    for (ssize_t bufSize = PATH_MAX/4; true; bufSize += bufSize/2) {
        buf.resize(bufSize);
        ssize_t rlSize = readlink(path.c_str(), buf.data(), bufSize);
        if (rlSize == -1)
            if (errno == EINVAL)
                throw Error("'%1%' is not a symlink", path);
            else
                throw PosixError("reading symbolic link '%1%'", path);
        else if (rlSize < bufSize)
            return string(buf.data(), rlSize);
    }
#else
    std::wstring wpath = pathW(path);
    DWORD dw = GetFileAttributesW(wpath.c_str());
    if (dw == 0xFFFFFFFF)
        throw WinError("readLink('%1%'): GetFileAttributesW when reading symbolic link", path);
    if ((dw & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        throw Error("'%1%' is not a symlink", path);

    AutoCloseWindowsHandle hFile = CreateFileW(wpath.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_POSIX_SEMANTICS|((dw & FILE_ATTRIBUTE_DIRECTORY) != 0 ? FILE_FLAG_BACKUP_SEMANTICS : 0), 0);
    if (hFile.get() == INVALID_HANDLE_VALUE)
        throw WinError("readLink('%1%'): CreateFileW when reading symbolic link", path);

    DWORD rd;
    char buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    if (!DeviceIoControl(hFile.get(), FSCTL_GET_REPARSE_POINT, 0, 0, buf, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &rd, NULL))
        throw WinError("readLink('%1%'): DeviceIoControl when reading symbolic link", path);

    const REPARSE_DATA_BUFFER* rdb = reinterpret_cast<const REPARSE_DATA_BUFFER*>(buf);
    assert(offsetof(REPARSE_DATA_BUFFER, MountPointReparseBuffer) + rdb->ReparseDataLength <= rd);
    std::wstring substituteName, printName;
    switch (rdb->ReparseTag) {
        case IO_REPARSE_TAG_MOUNT_POINT:
            substituteName = std::wstring((const wchar_t*)(((const char*)rdb->MountPointReparseBuffer  .PathBuffer)+rdb->MountPointReparseBuffer  .SubstituteNameOffset), rdb->MountPointReparseBuffer  .SubstituteNameLength/2);
            printName      = std::wstring((const wchar_t*)(((const char*)rdb->MountPointReparseBuffer  .PathBuffer)+rdb->MountPointReparseBuffer  .PrintNameOffset),      rdb->MountPointReparseBuffer  .PrintNameLength/2     );
            break;
        case IO_REPARSE_TAG_SYMLINK:
            substituteName = std::wstring((const wchar_t*)(((const char*)rdb->SymbolicLinkReparseBuffer.PathBuffer)+rdb->SymbolicLinkReparseBuffer.SubstituteNameOffset), rdb->SymbolicLinkReparseBuffer.SubstituteNameLength/2);
            printName      = std::wstring((const wchar_t*)(((const char*)rdb->SymbolicLinkReparseBuffer.PathBuffer)+rdb->SymbolicLinkReparseBuffer.PrintNameOffset),      rdb->SymbolicLinkReparseBuffer.PrintNameLength/2     );
            break;
        default:
            throw Error("readLink('%1%'): unknown reparse tag '%2$#x'", path, rdb->ReparseTag);
    }
    if (substituteName.size() > 4 && substituteName[0] == L'\\' && substituteName[1] == L'?' && substituteName[2] == L'?' && substituteName[3] == L'\\')
        return canonPath(to_bytes(substituteName.substr(4, substituteName.size()-4)));
    else
        return to_bytes(substituteName); // probably a ralative path. keeping it as it is important for calculation hashes of fixes-output derivations
#endif
}


bool isLink(const Path & path)
{
    return getFileType(path) == DT_LNK;
}

bool isDirectory(const Path & path)
{
    return getFileType(path) == DT_DIR;
}

#ifndef _WIN32
DirEntries readDirectory(const Path & path)
{
    DirEntries entries;
    entries.reserve(64);

    AutoCloseDir dir(opendir(path.c_str()));
    if (!dir) throw PosixError(format("opening directory '%1%'") % path);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
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
    if (errno) throw PosixError(format("reading directory '%1%'") % path);

    return entries;
}
#else
DirEntries readDirectory(const Path & path)
{
std::cerr << "readDirectory("<<path<<")"<<std::endl;
    DirEntries entries;
    entries.reserve(64);

    const std::wstring wpath = pathW(path);

    WIN32_FIND_DATAW wfd;
    HANDLE hFind = FindFirstFileExW((wpath + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        throw WinError("FindFirstFileExW when readDirectory '%1%'", path);
    } else {
        do {
            if ((wfd.cFileName[0] == '.' && wfd.cFileName[1] == '\0')
             || (wfd.cFileName[0] == '.' && wfd.cFileName[1] == '.' && wfd.cFileName[2] == '\0')) {
            } else {
                entries.emplace_back(DirEntry{wfd});
            }
        } while(FindNextFileW(hFind, &wfd));
        WinError winError("FindNextFileW when readDirectory '%1%'", path);
        if (winError.lastError != ERROR_NO_MORE_FILES)
            throw winError;
        FindClose(hFind);
    }

    return entries;
}
#endif


unsigned char getFileType(const Path & path)
{
#ifdef _WIN32
//std::cerr /*<< file<<":"<<line*/<<" __getFileType(" << path << ")" << std::endl;
    DWORD dw = GetFileAttributesW(pathW(path).c_str());
    if (dw == 0xFFFFFFFF)
        throw WinError("GetFileAttributesW when getFileType '%1%'", path);
    if (dw & FILE_ATTRIBUTE_REPARSE_POINT) { /*fprintf(stderr, "getFileType(%s) = DT_LNK %08X\n", path.c_str(), dw);*/ return DT_LNK; }
    if (dw & FILE_ATTRIBUTE_DIRECTORY    ) { /*fprintf(stderr, "getFileType(%s) = DT_DIR %08X\n", path.c_str(), dw);*/ return DT_DIR; }
    if (dw & FILE_ATTRIBUTE_NORMAL       ) { /*fprintf(stderr, "getFileType(%s) = DT_REG %08X\n", path.c_str(), dw);*/ return DT_REG; }
    if (dw & FILE_ATTRIBUTE_ARCHIVE      ) { /*fprintf(stderr, "getFileType(%s) = DT_REG %08X\n", path.c_str(), dw);*/ return DT_REG; }
    std::cerr /*<< file<<":"<<line*/<<" __getFileType(" << path << ") = DT_UNKNOWN (dw = "  << dw << ")" << std::endl;
    return DT_UNKNOWN;
#else
    struct stat st = lstatPath(path);
    if (S_ISDIR(st.st_mode)) return DT_DIR;
    if (S_ISLNK(st.st_mode)) return DT_LNK;
    if (S_ISREG(st.st_mode)) return DT_REG;
    return DT_UNKNOWN;
#endif
}

#ifndef _WIN32
string readFile(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        throw PosixError("statting file");

    std::vector<unsigned char> buf(st.st_size);
    readFull(fd, buf.data(), st.st_size);

    return string((char *) buf.data(), st.st_size);
}
#else
string readFile(HANDLE handle)
{
    LARGE_INTEGER li;
    if (!GetFileSizeEx(handle, &li))
        throw WinError("%s:%d statting file", __FILE__, __LINE__);

    std::vector<unsigned char> buf(li.QuadPart);
    readFull(handle, buf.data(), li.QuadPart);

    return string((char *) buf.data(), li.QuadPart);
}
#endif


string readFile(const Path & path, bool drain)
{
// std::cerr << "readFile("<<path<<", drain="<<drain<<")" << std::endl;
#ifndef _WIN32
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC );
    if (!fd)
        throw PosixError(format("opening file '%1%'") % path);
#else
//std::cerr << "readFile-1(" << path << ")" << std::endl;
    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when readFile '%1%' drain=%2%", path, drain);
#endif
    return drain ? drainFD(fd.get()) : readFile(fd.get());
}


void readFile(const Path & path, Sink & sink)
{
// std::cerr << "readFile("<<path<<")" << std::endl;
#ifndef _WIN32
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd) throw PosixError("opening file '%s'", path);
#else
//std::cerr << "readFile-2(" << path << ")" << std::endl;
    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when readFile '%1%'", path);
#endif
    drainFD(fd.get(), sink);
}


#ifndef _WIN32
void writeFile(const Path & path, const string & s, mode_t mode)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode);
    if (!fd)
        throw PosixError(format("opening file '%1%'") % path);
#else
void writeFile(const Path & path, const string & s, bool setReadOnlyAttribute)
{
    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                            (setReadOnlyAttribute ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL) | FILE_FLAG_POSIX_SEMANTICS,
                                            NULL);
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when writeFile '%1%' s.length=%2%", path, s.length());
#endif
    writeFull(fd.get(), s);
}


#ifndef _WIN32
void writeFile(const Path & path, Source & source, mode_t mode)
{
    AutoCloseFD fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode);
    if (!fd)
        throw PosixError(format("opening file '%1%'") % path);
#else
void writeFile(const Path & path, Source & source, bool setReadOnlyAttribute)
{
    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                            (setReadOnlyAttribute ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL) | FILE_FLAG_POSIX_SEMANTICS,
                                            NULL);
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when writeFile '%1%'", path);
#endif

    std::vector<unsigned char> buf(64 * 1024);

    while (true) {
        try {
            auto n = source.read(buf.data(), buf.size());
            writeFull(fd.get(), (unsigned char *) buf.data(), n);
        } catch (EndOfFile &) { break; }
    }
}

#ifndef _WIN32
string readLine(int fd)
{
    string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        ssize_t rd = read(fd, &ch, 1);
        if (rd == -1) {
            if (errno != EINTR)
                throw PosixError("reading a line");
        } else if (rd == 0)
            throw EndOfFile("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}
#else
string readLine(HANDLE handle)
{
    string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        DWORD rd;
        if (!ReadFile(handle, &ch, 1, &rd, NULL)) {
            throw WinError("reading a line");
        } else if (rd == 0)
            throw EndOfFile("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}
#endif

#ifndef _WIN32
void writeLine(int fd, string s)
{
    s += '\n';
    writeFull(fd, s);
}
#else
void writeLine(HANDLE handle, string s)
{
    s += '\n';
    writeFull(handle, s);
}
#endif

#ifndef _WIN32
static void _deletePath(const Path & path, unsigned long long & bytesFreed)
{
    checkInterrupt();

    struct stat st;
    if (stat(path.c_str(), &st) == -1) {
        if (errno == ENOENT) return;
        throw PosixError(format("getting status-3 of '%1%'") % path);
    }

    if (!S_ISDIR(st.st_mode) && st.st_nlink == 1)
        bytesFreed += st.st_blocks * 512;

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK) {
            if (chmod(path.c_str(), st.st_mode | PERM_MASK) == -1)
                throw PosixError(format("chmod '%1%'") % path);
        }
        for (auto & i : readDirectory(path))
            _deletePath(path + "/" + i.name, bytesFreed);
    }

    if (remove(path.c_str()) == -1) {
        if (errno == ENOENT) return;
        throw PosixError(format("cannot unlink '%1%'") % path);
    }
}

void deletePath(const Path & path, unsigned long long & bytesFreed)
{
    //Activity act(*logger, lvlDebug, format("recursively deleting path '%1%'") % path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed);
}

#else

static void _deletePath(const std::wstring & wpath, const DWORD * pdwFileAttributes, const uint64_t * pFilesize, unsigned long long & bytesFreed)
{
//  std::cerr << "_deletePath(" << to_bytes(wpath) << ")" << std::endl;

    checkInterrupt();

    DWORD dwFileAttributes;
    uint64_t filesize;
    if (pdwFileAttributes == NULL || pFilesize == NULL) {
        WIN32_FILE_ATTRIBUTE_DATA wfad;
        if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &wfad)) {
            WinError winError("GetFileAttributesExW when _deletePath '%1%'", to_bytes(wpath));
            if (winError.lastError == ERROR_FILE_NOT_FOUND)
                return;
            throw winError;
        }
        dwFileAttributes = wfad.dwFileAttributes;
        filesize = (uint64_t(wfad.nFileSizeHigh) << 32) + wfad.nFileSizeLow;
    } else {
        dwFileAttributes = *pdwFileAttributes;
        filesize = *pFilesize;
    }

    /* Make the directory or file accessible. */
    if ((dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        if (!SetFileAttributesW(wpath.c_str(), dwFileAttributes & ~FILE_ATTRIBUTE_READONLY))
            throw WinError("SetFileAttributesW when _deletePath '%1%'", to_bytes(wpath));
    }

    if ((dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            WIN32_FIND_DATAW wfd;
            HANDLE hFind = FindFirstFileExW((wpath + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
            if (hFind == INVALID_HANDLE_VALUE) {
                throw WinError("FindFirstFileExW when deletePath '%1%'", to_bytes(wpath));
            } else {
                do {
                    if ((wfd.cFileName[0] == '.' && wfd.cFileName[1] == '\0')
                     || (wfd.cFileName[0] == '.' && wfd.cFileName[1] == '.' && wfd.cFileName[2] == '\0')) {
                    } else {
                        uint64_t fs = (uint64_t(wfd.nFileSizeHigh) << 32) + wfd.nFileSizeLow;
                        _deletePath(wpath + L"\\" + wfd.cFileName, &wfd.dwFileAttributes, &fs, bytesFreed);
                    }
                } while(FindNextFileW(hFind, &wfd));
                WinError winError("FindNextFileW when deletePath '%1%'", to_bytes(wpath));
                if (winError.lastError != ERROR_NO_MORE_FILES)
                    throw winError;
                FindClose(hFind);
            }
        }
        if (!RemoveDirectoryW(wpath.c_str())) {
            // RemoveDirectory fails on Windows more often than on POSIX.
            // For example when the directory is the currrent in another process.
            // Thus it shouldn't be fatal error.
            WinError winError("RemoveDirectoryW  when _deletePath '%1%' dwFileAttributes=%2%", to_bytes(wpath), dwFileAttributes);
            std::cerr << winError.msg() << std::endl;
        }
    } else {
        if (!DeleteFileW(wpath.c_str())) {
            WinError winError("DeleteFileW  when _deletePath '%1%' dwFileAttributes=%2%", to_bytes(wpath), dwFileAttributes);
            if (winError.lastError != ERROR_FILE_NOT_FOUND) // it happens with .tmp files
                throw winError;
        }
        bytesFreed += filesize;
    }
}

void deletePath(const Path & path, unsigned long long & bytesFreed)
{
    std::cerr << "deletePath("<<path<<")"<<std::endl;
    //Activity act(*logger, lvlDebug, format("recursively deleting path '%1%'") % path);
    bytesFreed = 0;
    _deletePath(pathW(path), nullptr, nullptr, bytesFreed);
}
#endif


void deletePath(const Path & path)
{
    unsigned long long dummy;
    deletePath(path, dummy);
}



static Path tempName(Path tmpRoot, const Path & prefix, bool includePid,
    int & counter)
{
//std::cerr << "tempName("<<tmpRoot<<", "<<prefix<<", includePid="<<includePid<<", counter="<<counter<<")"<<std::endl;
#ifndef _WIN32
    if (tmpRoot.empty()) tmpRoot = getEnv("TMPDIR", "/tmp");
    tmpRoot = canonPath(tmpRoot, true);
    if (includePid)
        return (format("%1%/%2%-%3%-%4%") % tmpRoot % prefix % getpid() % counter++).str();
    else
        return (format("%1%/%2%-%3%") % tmpRoot % prefix % counter++).str();
#else
    if (tmpRoot.empty()) tmpRoot = getEnv("TMPDIR");
    if (tmpRoot.empty()) tmpRoot = getEnv("TMP");
    if (tmpRoot.empty()) tmpRoot = getEnv("TEMP");
    if (tmpRoot.empty()) throw Error("both '%TEMP%' and '%TMP%' are empty");
    tmpRoot = canonPath(tmpRoot, true);
    if (includePid)
        return (format("%1%/%2%-%3%-%4%") % tmpRoot % prefix % GetCurrentProcessId() % counter++).str();
    else
        return (format("%1%/%2%-%3%") % tmpRoot % prefix % counter++).str();
#endif
}


#ifndef _WIN32
Path createTempDir(const Path & tmpRoot, const Path & prefix, bool includePid, bool useGlobalCounter, mode_t mode)
{
//std::cerr << "createTempDir("<<tmpRoot<<", "<<prefix<<", includePid="<<includePid<<", useGlobalCounter="<<useGlobalCounter<<")"<<std::endl;
    static int globalCounter = 0;
    int localCounter = 0;
    int & counter(useGlobalCounter ? globalCounter : localCounter);

    while (1) {
        checkInterrupt();
        Path tmpDir = tempName(tmpRoot, prefix, includePid, counter);
        if (mkdir(tmpDir.c_str(), mode) == 0) {
#if __FreeBSD__
            /* Explicitly set the group of the directory.  This is to
               work around around problems caused by BSD's group
               ownership semantics (directories inherit the group of
               the parent).  For instance, the group of /tmp on
               FreeBSD is "wheel", so all directories created in /tmp
               will be owned by "wheel"; but if the user is not in
               "wheel", then "tar" will fail to unpack archives that
               have the setgid bit set on directories. */
            if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
                throw PosixError(format("setting group of directory '%1%'") % tmpDir);
#endif
            return tmpDir;
        }
        if (errno != EEXIST)
            throw PosixError(format("creating directory '%1%'") % tmpDir);
    }
}
#else
Path createTempDir(const Path & tmpRoot, const Path & prefix, bool includePid, bool useGlobalCounter)
{
//std::cerr << "createTempDir("<<tmpRoot<<", "<<prefix<<", includePid="<<includePid<<", useGlobalCounter="<<useGlobalCounter<<")"<<std::endl;
    static int globalCounter = 0;
    int localCounter = 0;
    int & counter(useGlobalCounter ? globalCounter : localCounter);

    while (1) {
        checkInterrupt();
        Path tmpDir = tempName(tmpRoot, prefix, includePid, counter);
        if (CreateDirectoryW(pathW(tmpDir).c_str(), NULL)) {
            return tmpDir;
        }
        WinError winError("CreateDirectoryW when createTempDir '%1%'", tmpDir);
        if (winError.lastError != ERROR_ALREADY_EXISTS && winError.lastError != ERROR_ACCESS_DENIED)
            throw winError;
    }
}
#endif


static Lazy<Path> getHome2([]() {
#ifndef _WIN32
    Path homeDir = getEnv("HOME");
    if (homeDir.empty()) {
        std::vector<char> buf(16384);
        struct passwd pwbuf;
        struct passwd * pw;
        if (getpwuid_r(geteuid(), &pwbuf, buf.data(), buf.size(), &pw) != 0
            || !pw || !pw->pw_dir || !pw->pw_dir[0])
            throw Error("cannot determine user's home directory");
        homeDir = pw->pw_dir;
    }
#else
    Path homeDir = getEnv("USERPROFILE");
    assert(!homeDir.empty());
    homeDir = canonPath(homeDir);
#endif
    return homeDir;
});

Path getHome() { return getHome2(); }


Path getCacheDir()
{
    Path cacheDir = getEnv("XDG_CACHE_HOME");
    if (cacheDir.empty())
        cacheDir = getHome() + "/.cache";
    return cacheDir;
}


Path getConfigDir()
{
    Path configDir = getEnv("XDG_CONFIG_HOME");
    if (configDir.empty())
        configDir = getHome() + "/.config";
    return configDir;
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    string configDirs = getEnv("XDG_CONFIG_DIRS");
    std::vector<Path> result = tokenizeString<std::vector<string>>(configDirs, ":");
    result.insert(result.begin(), configHome);
    return result;
}


Path getDataDir()
{
    Path dataDir = getEnv("XDG_DATA_HOME");
    if (dataDir.empty())
        dataDir = getHome() + "/.local/share";
    return dataDir;
}


Paths createDirs(const Path & path)
{
    Paths created;

#ifndef _WIN32
    if (path == "/") return created;
    assert(!path.empty() && path[path.size()-1] != '/');

    struct stat st;
    if (lstat(path.c_str(), &st) == -1) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str(), 0777) == -1 && errno != EEXIST)
            throw PosixError(format("creating directory '%1%'") % path);
        st = lstatPath(path);
        created.push_back(path);
    }

    if (S_ISLNK(st.st_mode) && stat(path.c_str(), &st) == -1)
        throw PosixError(format("statting symlink '%1%'") % path);

    if (!S_ISDIR(st.st_mode)) throw Error(format("'%1%' is not a directory") % path);
#else
//std::cerr << "createDirs(" << path << ")" << std::endl;
    const std::wstring wpath = pathW(path);
    DWORD dw = GetFileAttributesW(wpath.c_str());

    if (dw == 0xFFFFFFFF) {
        WinError winError("GetFileAttributesW when createDirs '%1%'", path);
        switch (winError.lastError) {
            case ERROR_PATH_NOT_FOUND:
                created = createDirs(dirOf(path));
                break;
            case ERROR_FILE_NOT_FOUND:
                break;
            default:
                throw winError;
        }
        if (!CreateDirectoryW(wpath.c_str(), NULL)) {
            WinError winError("CreateDirectoryW when createDirs '%1%'", path);
            if (winError.lastError != ERROR_ALREADY_EXISTS)
                throw winError;
        }
        dw = GetFileAttributesW(wpath.c_str());
        if (dw == 0xFFFFFFFF)
            throw WinError("GetFileAttributesW after CreateDirectoryW of '%1%'", path);
        created.push_back(path);
    }

    if (dw & FILE_ATTRIBUTE_REPARSE_POINT) {
        std::cerr << "todo: follow symlink? '"<<path<<"' dw="<<dw<<std::endl;
        //_exit(112);
    }

    if (!(dw & FILE_ATTRIBUTE_DIRECTORY)) {
        throw Error(format("'%1%' is not a directory") % path);
    }
#endif

    return created;
}


#ifndef _WIN32
void createSymlink(const Path & target, const Path & link)
{
    assert(!target.empty() && !link.empty());
    if (symlink(target.c_str(), link.c_str()))
        throw PosixError(format("creating symlink from '%1%' to '%2%'") % link % target);
}
#else
SymlinkType createSymlink(const Path & target, const Path & link)
{
    assert(!target.empty() && !link.empty());
    assert(target[0] != '/' && link[0] != '/');
    BOOLEAN rc;
    std::wstring wlink = pathW(link);
    std::optional<std::wstring> wtarget = maybePathW(target);
    SymlinkType st;
    if (!wtarget) {
        std::cerr << "path not absolute (" << target  << "), so make a dangling symlink" << std::endl;
        std::wstring danglingTarget = from_bytes(target);
        std::replace(danglingTarget.begin(), danglingTarget.end(), '/', '\\');
        rc = CreateSymbolicLinkW(wlink.c_str(), danglingTarget.c_str(), 0);
        st = SymlinkTypeDangling;
    } else {
        while (wtarget->size() >= 2 && (*wtarget)[wtarget->size()-2] == '\\' && (*wtarget)[wtarget->size()-1] == '.') { // links to "c:\blablabla\." do not work well
          wtarget = wtarget->substr(0, wtarget->size()-2);
        }
        DWORD dw = GetFileAttributesW(wtarget->c_str()); // fails on
        if (dw == 0xFFFFFFFF) {
            std::cerr << "GetFileAttributesW(" << to_bytes(*wtarget)  << ") failed with "<<GetLastError()<<", so make a dangling symlink" << std::endl;
            std::wstring danglingTarget = from_bytes(target);
            std::replace(danglingTarget.begin(), danglingTarget.end(), '/', '\\');
            rc = CreateSymbolicLinkW(wlink.c_str(), danglingTarget.c_str(), 0);
            st = SymlinkTypeDangling;
        } else if (dw & FILE_ATTRIBUTE_DIRECTORY) {
            rc = CreateSymbolicLinkW(wlink.c_str(), wtarget->c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY);
            st = SymlinkTypeDirectory;
        } else {
            rc = CreateSymbolicLinkW(wlink.c_str(), wtarget->c_str(), 0);
            st = SymlinkTypeFile;
        }
    }
    if (!rc) {
        WinError winError("creating symlink from '%1%' to '%2%'", link, target);
        if (winError.lastError == ERROR_PRIVILEGE_NOT_HELD) {
            fprintf(stderr, "TODO: wscalate UAC and add privilege\n");fflush(stderr);
            _exit(113);
        }
        fprintf(stderr, "CreateSymbolicLinkW failed code=%ld\n", winError.lastError);
        throw winError;
    }
    return st;
}
#endif

void replaceSymlink(const Path & target, const Path & link)
{
    assert(!target.empty() && !link.empty());
#ifdef _WIN32
    assert(target[0] != '/' && link[0] != '/');
#endif
    for (unsigned int n = 0; true; n++) {
        Path tmp = canonPath(fmt("%s/.%d_%s", dirOf(link), n, baseNameOf(link)));

#ifndef _WIN32
        try {
            createSymlink(target, tmp);
        } catch (PosixError & e) {
            if (e.errNo == EEXIST) continue;
            throw;
        }

        if (rename(tmp.c_str(), link.c_str()) != 0)
            throw PosixError(format("renaming '%1%' to '%2%'") % tmp % link);
#else
        SymlinkType st;
        try {
            st = createSymlink(target, tmp);
        } catch (WinError & e) {
            if (e.lastError == ERROR_ALREADY_EXISTS) continue;
            throw;
        }

        if (!MoveFileExW(pathW(tmp).c_str(), pathW(link).c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            // MOVEFILE_REPLACE_EXISTING might fail with access denied, so try once more harder (atomicity suffers here)
            std::wstring old = pathW(absPath((format("%1%.old~%2%-%3%") % link % GetCurrentProcessId() % random()).str()));
            if (!MoveFileExW(pathW(link).c_str(), old.c_str(), MOVEFILE_WRITE_THROUGH))
                throw WinError("MoveFileExW '%1%' -> '%2%'", link, to_bytes(old));
            // repeat
            if (!MoveFileExW(pathW(tmp).c_str(), pathW(link).c_str(), MOVEFILE_WRITE_THROUGH))
                throw WinError("MoveFileExW '%1%' -> '%2%'", tmp, link);

            DWORD dw = GetFileAttributesW(old.c_str());
            if (dw == 0xFFFFFFFF)
                throw WinError("GetFileAttributesW '%1%'", to_bytes(old));
            if ((dw & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!RemoveDirectoryW(old.c_str()))
                    std::cerr << WinError("RemoveDirectoryW '%1%'", to_bytes(old)).msg() << std::endl;
            } else {
                if (!DeleteFileW(old.c_str()))
                    std::cerr << WinError("DeleteFileW '%1%'", to_bytes(old)).msg() << std::endl;
            }
        }
#endif
        break;
    }
}

#ifndef _WIN32
void readFull(int fd, unsigned char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, (char *) buf, count);
        if (res == -1) {
            if (errno == EINTR) continue;
            throw PosixError("reading from file");
        }
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}
#else
void readFull(HANDLE handle, unsigned char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        DWORD res;
        if (!ReadFile(handle, (char *) buf, count, &res, NULL))
            throw WinError("%s:%d reading from file", __FILE__, __LINE__);
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}
#endif


#ifndef _WIN32
void writeFull(int fd, const unsigned char * buf, size_t count, bool allowInterrupts)
{
    while (count) {
        if (allowInterrupts) checkInterrupt();
        ssize_t res = write(fd, (char *) buf, count);
        if (res == -1 && errno != EINTR)
            throw PosixError("writing to file");
        if (res > 0) {
            count -= res;
            buf += res;
        }
    }
}
#else
void writeFull(HANDLE handle, const unsigned char * buf, size_t count, bool allowInterrupts)
{
    while (count) {
        if (allowInterrupts) checkInterrupt();
        DWORD res;
        auto path = handleToPath(handle); // debug; do it before becuase handleToPath changes lasterror
        if (!WriteFile(handle, (char *) buf, count, &res, NULL)) {
            throw WinError("writing to file %1%:%2%", handle, path);
        }
        if (res > 0) {
            count -= res;
            buf += res;
        }
    }
}
#endif


#ifndef _WIN32
void writeFull(int fd, const string & s, bool allowInterrupts)
{
    writeFull(fd, (const unsigned char *) s.data(), s.size(), allowInterrupts);
}
#else
void writeFull(HANDLE handle, const string & s, bool allowInterrupts)
{
    writeFull(handle, (const unsigned char *) s.data(), s.size(), allowInterrupts);
}
#endif


#ifndef _WIN32
string drainFD(int fd, bool block)
{
    StringSink sink;
    drainFD(fd, sink, block);
    return std::move(*sink.s);
}
#else
string drainFD(HANDLE fd/*, bool block*/)
{
    StringSink sink;
    drainFD(fd, sink/*, block*/);
    return std::move(*sink.s);
}
#endif


#ifndef _WIN32
void drainFD(int fd, Sink & sink, bool block)
{
    int saved;

    Finally finally([&]() {
        if (!block) {
            if (fcntl(fd, F_SETFL, saved) == -1)
                throw PosixError("making file descriptor blocking");
        }
    });

    if (!block) {
        saved = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, saved | O_NONBLOCK) == -1)
            throw PosixError("making file descriptor non-blocking");
    }

    std::vector<unsigned char> buf(64 * 1024);
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buf.data(), buf.size());
        if (rd == -1) {
            if (!block && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (errno != EINTR)
                throw PosixError("reading from file");
        }
        else if (rd == 0) break;
        else sink(buf.data(), rd);
    }
}
#else
void drainFD(HANDLE handle, Sink & sink/*, bool block*/)
{
    std::vector<unsigned char> buf(64 * 1024);
    while (1) {
        checkInterrupt();
        DWORD rd;
        if (!ReadFile(handle, buf.data(), buf.size(), &rd, NULL)) {
            WinError winError("%s:%d reading from handle %p", __FILE__, __LINE__, handle);
            if (winError.lastError == ERROR_BROKEN_PIPE)
                break;
            throw winError;
        }
        else if (rd == 0) break;
        sink(buf.data(), rd);
    }
}
#endif


//////////////////////////////////////////////////////////////////////


AutoDelete::AutoDelete() : del{false} {}

AutoDelete::AutoDelete(const Path & p, bool recursive) : path(p)
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
#ifndef _WIN32
                if (remove(path.c_str()) == -1)
                    throw PosixError(format("cannot unlink '%1%'") % path);
#else
                if (!DeleteFileW(pathW(path).c_str()))
                    throw WinError("DeleteFileW '%1%'", path);
#endif
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

#ifndef _WIN32
AutoCloseFD::AutoCloseFD() : fd{-1} {}


AutoCloseFD::AutoCloseFD(int fd) : fd{fd} {}


AutoCloseFD::AutoCloseFD(AutoCloseFD&& that) : fd{that.fd}
{
    that.fd = -1;
}


AutoCloseFD& AutoCloseFD::operator =(AutoCloseFD&& that)
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
            throw PosixError(format("closing file descriptor %1%") % fd);
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

#else

AutoCloseWindowsHandle::AutoCloseWindowsHandle() : handle{INVALID_HANDLE_VALUE} {}


AutoCloseWindowsHandle::AutoCloseWindowsHandle(HANDLE handle) : handle{handle} {}


AutoCloseWindowsHandle::AutoCloseWindowsHandle(AutoCloseWindowsHandle&& that) : handle{that.handle}
{
    that.handle = INVALID_HANDLE_VALUE;
}


AutoCloseWindowsHandle& AutoCloseWindowsHandle::operator =(AutoCloseWindowsHandle&& that)
{
    close();
    handle = that.handle;
    that.handle = INVALID_HANDLE_VALUE;
    return *this;
}


AutoCloseWindowsHandle::~AutoCloseWindowsHandle()
{
    try {
        close();
    } catch (...) {
        ignoreException();
    }
}


HANDLE AutoCloseWindowsHandle::get() const
{
    return handle;
}


void AutoCloseWindowsHandle::close()
{
    if (handle != INVALID_HANDLE_VALUE) {
        if (::CloseHandle(handle) == -1)
            /* This should never happen. */
            throw PosixError(format("closing file descriptor %1%") % handle);
    }
}


AutoCloseWindowsHandle::operator bool() const
{
    return handle != INVALID_HANDLE_VALUE;
}


HANDLE AutoCloseWindowsHandle::release()
{
    HANDLE oldFD = handle;
    handle = INVALID_HANDLE_VALUE;
    return oldFD;
}
#endif

#ifdef _WIN32
void AsyncPipe::createAsyncPipe(HANDLE iocp) {
//std::cerr << (format("-----AsyncPipe::createAsyncPipe(%x)") % iocp) << std::endl;

    buffer.resize(0x1000);
    memset(&overlapped, 0, sizeof(overlapped));

    std::string pipeName = (format("\\\\.\\pipe\\nix-%d-%p") % GetCurrentProcessId() % (void*)this).str();

    hRead = CreateNamedPipeA(pipeName.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE,
                                        PIPE_UNLIMITED_INSTANCES, 0, 0, INFINITE, NULL);
    if (!hRead)
        throw WinError("CreateNamedPipeA(%s)", pipeName);

    HANDLE hIocp = CreateIoCompletionPort(hRead.get(), iocp, (ULONG_PTR)(hRead.get()) ^ 0x5555, 0);
    if (hIocp != iocp)
        throw WinError("CreateIoCompletionPort(%x[%s], %x, ...) returned %x", hRead.get(), pipeName, iocp, hIocp);

    if (!ConnectNamedPipe(hRead.get(), &overlapped) && GetLastError() != ERROR_IO_PENDING)
        throw WinError("ConnectNamedPipe(%s)", pipeName);

    SECURITY_ATTRIBUTES psa2 = {0};
    psa2.nLength = sizeof(SECURITY_ATTRIBUTES);
    psa2.bInheritHandle = TRUE;

    hWrite = CreateFileA(pipeName.c_str(), GENERIC_WRITE, 0, &psa2, OPEN_EXISTING, 0, NULL);
    if (!hRead)
        throw WinError("CreateFileA(%s)", pipeName);
}
#endif

#ifndef _WIN32
void Pipe::create()
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC) != 0) throw PosixError("creating pipe");
#else
    if (pipe(fds) != 0) throw PosixError("creating pipe");
    closeOnExec(fds[0]);
    closeOnExec(fds[1]);
#endif
    readSide = fds[0];
    writeSide = fds[1];
}
#else

void Pipe::createPipe()
{
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0))
        throw WinError("CreatePipe");

    hRead = hReadPipe;
    hWrite = hWritePipe;
}
#endif


//////////////////////////////////////////////////////////////////////


#ifndef _WIN32
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

    debug(format("killing process %1%") % pid);

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
            printError((SysError("killing process %d", pid).msg()));
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
            throw PosixError("cannot get child exit status");
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
#else
Pid::Pid(): hProcess(INVALID_HANDLE_VALUE), dwProcessId(DWORD(-1))
{
}

Pid::~Pid()
{
//std::cerr << "Pid::~Pid()" << std::endl;
    if (hProcess != INVALID_HANDLE_VALUE) kill();
}

void Pid::set(HANDLE hProcess, DWORD dwProcessId)
{
//std::cerr << "Pid::set("<<hProcess<<","<<dwProcessId<<") old=("<<this->hProcess<<","<<this->dwProcessId<<")" << std::endl;
    assert(this->hProcess == INVALID_HANDLE_VALUE && this->dwProcessId == DWORD(-1) && hProcess != INVALID_HANDLE_VALUE && dwProcessId != DWORD(-1));
    this->hProcess = hProcess;
    this->dwProcessId = dwProcessId;
}


int Pid::kill()
{
    assert(hProcess != INVALID_HANDLE_VALUE);

std::cerr << (format("killing process %1%") % dwProcessId) << std::endl;
    debug(format("killing process %1%") % dwProcessId);

    switch (WaitForSingleObject(hProcess, 1000)) { // wait 1s for good finish to get a valid exit code (how small the timeout could be?; build.cc calls Pid::kill as soon as stdout pipe ended)
    case WAIT_TIMEOUT:
        std::cerr << (format("Pid::kill(): WaitForSingleObject(%1% [%2%]) -> TIMEOUT") % hProcess % dwProcessId) << std::endl;
        if (!TerminateProcess(hProcess, 177)) {
            std::cerr << ((WinError("Pid::kill(): TerminateProcess(%1% [%2%])", hProcess, dwProcessId).msg())) << std::endl;
        }
        return wait();

    case WAIT_OBJECT_0: {
        std::cerr << (format("Pid::kill(): WaitForSingleObject(%1% [%2%]) -> WAIT_OBJECT_0") % hProcess % dwProcessId) << std::endl;
        DWORD dwExitCode = 176;
        if (!GetExitCodeProcess(hProcess, &dwExitCode)) {
            std::cerr << ((WinError("Pid::kill(): GetExitCodeProcess(%1% [%2%])", hProcess, dwProcessId).msg())) << std::endl;
        }
        std::cerr << (format("Pid::kill(): GetExitCodeProcess(%1% [%2%]) -> %3%") % hProcess % dwProcessId % dwExitCode) << std::endl;
        CloseHandle(hProcess);
        hProcess = INVALID_HANDLE_VALUE;
        dwProcessId = DWORD(-1);
        return dwExitCode;
    }

    default:
        throw WinError("WaitForSingleObject(%1% [%2%]) - WTF", hProcess, dwProcessId);
    }
}

int Pid::wait()
{
    switch (WaitForSingleObject(hProcess, INFINITE)) {
    case WAIT_OBJECT_0:
        break;
    default:
        std::cerr << ((WinError("Pid::wait(): WaitForSingleObject(%1% [%2%])", hProcess, dwProcessId).msg())) << std::endl;
    }

    DWORD dwExitCode = 176;
    if (!GetExitCodeProcess(hProcess, &dwExitCode)) {
        std::cerr << ((WinError("Pid::wait(): GetExitCodeProcess(%1% [%2%])", hProcess, dwProcessId).msg())) << std::endl;
    } else {
        std::cerr << (format("Pid::wait(): GetExitCodeProcess(%1% [%2%]) -> %3%") % hProcess % dwProcessId % dwExitCode).str() << std::endl;
    }

    CloseHandle(hProcess);
    hProcess = INVALID_HANDLE_VALUE;
    dwProcessId = DWORD(-1);
    return dwExitCode;
}
#endif


#ifndef _WIN32
void killUser(uid_t uid)
{
    debug(format("killing all processes running under uid '%1%'") % uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

    ProcessOptions options;
    options.allowVfork = false;

    Pid pid = startProcess([&]() {

        if (setuid(uid) == -1)
            throw PosixError("setting uid");

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
                throw PosixError(format("cannot kill processes for uid '%1%'") % uid);
        }

        _exit(0);
    }, options);

    int status = pid.wait();
    if (status != 0)
        throw Error(format("cannot kill processes for uid '%1%': %2%") % uid % statusToString(status));

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
}
#endif

//////////////////////////////////////////////////////////////////////

#ifndef _WIN32
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
        if (!options.allowVfork)
            logger = makeDefaultLogger();
        try {
#if __linux__
            if (options.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
                throw PosixError("setting death signal");
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
    if (pid == -1) throw PosixError("unable to fork");

    return pid;
}
#endif

std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    for (auto & s : ss) res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}


string runProgramGetStdout(Path program, bool searchPath, const Strings & args,
    const std::optional<std::string> & input)
{
    RunOptions opts(program, args);
    opts.searchPath = searchPath;
    opts.input = input;

    std::pair<int, std::string> res = runProgramWithOptions(opts);

    if (!statusOk(res.first))
        throw ExecError(res.first, fmt("program '%1%' %2%", program, statusToString(res.first)));

    return res.second;
}

void runProgram2(const RunOptions & options);

std::pair<int, std::string> runProgramWithOptions(const RunOptions & options_)
{
    RunOptions options(options_);
    StringSink sink;
    options.standardOut = &sink;

    int status = 0;

    try {
        runProgram2(options);
    } catch (ExecError & e) {
        status = e.status;
    }

    return {status, std::move(*sink.s)};
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

#ifndef _WIN32
    /* Create a pipe. */
    Pipe out, in;
    if (options.standardOut) out.createPipe();
    if (source) in.createPipe();

    /* Fork. */
    Pid pid = startProcess([&]() {
        if (options.standardOut && dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw PosixError("dupping stdout");
        if (source && dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw PosixError("dupping stdin");

        Strings args_(options.args);
        args_.push_front(options.program);

        restoreSignals();

        if (options.searchPath)
            execvp(options.program.c_str(), stringsToCharPtrs(args_).data());
        else
            execv(options.program.c_str(), stringsToCharPtrs(args_).data());

        throw PosixError("executing '%1%'", options.program);
    });
    out.writeSide = -1;
#else

{
std::string args;
for (auto & s : options.args) {
    if (!args.empty()) args += " ";
    args += windowsEscape(s);
}
std::cerr << __FILE__ << ":" << __LINE__
    << " standardIn='"  << options.standardIn << "'"
    << " standardOut='" << options.standardOut << "'"
    << " program='"     << options.program << "'"
    << " args='"        << args << "'"
    << " searchPath='"  << options.searchPath << "'"
    << std::endl;
if (options.input)
    std::cerr << "input='" << *options.input << "'" << std::endl;
}

    Pid pid;
    Pipe in, out;

    std::map<std::wstring, std::wstring> uenv;
//  uenv[L"HOME"]        = getEnvW(L"HOME",        L"");
    uenv[L"USERPROFILE"] = getEnvW(L"USERPROFILE", L"");
    uenv[L"TEMP"]        = getEnvW(L"TEMP",        L"");
    uenv[L"PATH"]        = getEnvW(L"PATH",        L"");
//  std::cerr << "HOME="        << to_bytes(uenv[L"HOME"])        << std::endl;
    std::cerr << "USERPROFILE=" << to_bytes(uenv[L"USERPROFILE"]) << std::endl;
    std::cerr << "TEMP="        << to_bytes(uenv[L"TEMP"])        << std::endl;
    std::cerr << "PATH="        << to_bytes(uenv[L"PATH"])        << std::endl;

    std::wstring uenvline;
    for (auto & i : uenv)
        uenvline += i.first + L'=' + i.second + L'\0';
    uenvline += L'\0';

    STARTUPINFOW si = {0};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;

    AutoCloseWindowsHandle nul;
    if (!source || !options.standardOut) {
        SECURITY_ATTRIBUTES sa = {0};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_EXISTING, 0, NULL);
        if (!nul.get())
            throw WinError("CreateFileA(NUL)");
    }

    if (source) {
        in.createPipe();
        if (!SetHandleInformation(in.hWrite.get(), HANDLE_FLAG_INHERIT, 0)) // do not inherit the write end
            throw WinError("SetHandleInformation");
        si.hStdInput = in.hRead.get();
    } else {
        si.hStdInput = nul.get();
    }
    if (options.standardOut) {
        out.createPipe();
        if (!SetHandleInformation(out.hRead.get(), HANDLE_FLAG_INHERIT, 0)) // do not inherit the read end
            throw WinError("SetHandleInformation");
        si.hStdOutput = out.hWrite.get();
        si.hStdError = out.hWrite.get();
    } else {
        si.hStdOutput = nul.get();
        si.hStdError = nul.get();
    }



    /*
     * try to read executable and find shebangs (needed for hg)
     */
    Path executable;
    if (!options.searchPath) {
        executable = options.program;
        if (!pathExists(executable))
            throw Error("executable '%1%' not found", executable);
    } else if ( (options.program.length() >= 7 && options.program[0] == '\\')
             || (options.program.length() >= 4 && options.program[1] == ':' && isslash(options.program[1]))) {
        executable = options.program;
        if (!pathExists(executable))
            throw Error("executable '%1%' not found", executable);
    } else {
        assert(options.program.find('/') == string::npos);
        assert(options.program.find('\\') == string::npos);

        bool found = false;
        for (const std::string & subpath : tokenizeString<Strings>(getEnv("PATH", ""), ";")) {
            Path candidate = canonPath(subpath) + '/' + options.program;
            if (pathExists(candidate       )) { executable = candidate       ; found = true; break; }
            if (pathExists(candidate+".exe")) { executable = candidate+".exe"; found = true; break; }
            if (pathExists(candidate+".cmd")) { executable = candidate+".cmd"; found = true; break; }
            if (pathExists(candidate+".bat")) { executable = candidate+".bat"; found = true; break; }
        }
        if (!found)
            throw Error("executable '%1%' not found on PATH", executable);
    }

    string shebang;
    if ( !boost::algorithm::iends_with(executable, ".exe")
      && !boost::algorithm::iends_with(executable, ".cmd")
      && !boost::algorithm::iends_with(executable, ".bat")
       ) {
      std::vector<char> buf(512);
      {
          AutoCloseWindowsHandle fd = CreateFileW(pathW(executable).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
          if (fd.get() == INVALID_HANDLE_VALUE)
              throw WinError("CreateFileW '%1%'", executable);

          DWORD filled = 0;
          while (filled < buf.size()) {
              DWORD n;
              if (!ReadFile(fd.get(), buf.data() + filled, buf.size() - filled, &n, NULL))
                  throw WinError("ReadFile '%1%'", executable);
              if (n == 0)
                  break;
              filled += n;
          }
          buf.resize(filled);
      }
      if (buf.size() < 3)
          throw Error("executable '%1%' is too small", executable);
      if (buf[0] == '#' && buf[1] == '!') {
          std::vector<char>::const_iterator lf = std::find(buf.begin(), buf.end(), '\n');
          if (lf == buf.end())
              throw Error("executable '%1%' shebang is too long", executable);

          shebang = trim(std::string(buf.data()+2, lf-buf.begin()-2));
//        std::cerr << "_________________shebang1='" << shebang << "'" << std::endl;

          if (shebang.empty())
              throw Error("executable '%1%' shebang is empty", executable);

          // BUGBUG: msys hack, remove later
          if (shebang[0] == '/') {
              shebang = trim(runProgramGetStdout("cygpath", true, {"-m", shebang}));
//            std::cerr << "_________________shebang2='" << shebang << "'" << std::endl;
          }
          assert(!shebang.empty() && shebang[0] != '/');

          if (!pathExists(shebang))
              throw Error("executable '%1%' shebang '%2%' does not exist", executable, shebang);
      }
    }

    std::wstring ucmdline;
    if (!shebang.empty())
        ucmdline = windowsEscapeW(from_bytes(shebang)) + L' ';
    ucmdline += windowsEscapeW(from_bytes(executable));
    for (const auto & v : options.args) {
        ucmdline += L' ';
        ucmdline += windowsEscapeW(from_bytes(v));
    }

    std::cerr << "_________________executable='" << to_bytes(pathW(executable)) << "'" << std::endl;
    std::cerr << "_________________shebang='"    <<         (shebang)           << "'" << std::endl;
    std::cerr << "_________________ucmdline='"   << to_bytes(ucmdline)          << "'" << std::endl;

    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(
        pathW(shebang.empty() ? executable : shebang).c_str(),         // LPCWSTR               lpApplicationName,
        const_cast<wchar_t*>((ucmdline).c_str()),                      // LPWSTR                lpCommandLine,
        NULL,                                                          // LPSECURITY_ATTRIBUTES lpProcessAttributes,
        NULL,                                                          // LPSECURITY_ATTRIBUTES lpThreadAttributes,
        TRUE,                                                          // BOOL                  bInheritHandles,
        CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,                 // DWORD                 dwCreationFlags,
        const_cast<wchar_t*>(uenvline.c_str()),
        /*from_bytes(rewriteStrings(env["PWD"], inputRewrites)).c_str()*/NULL, // LPCWSTR               lpCurrentDirectory,
        &si,                                                           // LPSTARTUPINFOW        lpStartupInfo,
        &pi                                                            // LPPROCESS_INFORMATION lpProcessInformation
    )) {
        throw WinError("CreateProcessW(%1%)", to_bytes(ucmdline));
    }
//  std::cerr << to_bytes(ucmdline) << "   pi.hProcess=" << pi.hProcess << "\n";

    // to kill the child process on parent death (hJob can be reused?)
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    if (hJob == NULL) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        throw WinError("CreateJobObjectA()");
    }
    if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        throw WinError("AssignProcessToJobObject()");
    }
    if (!ResumeThread(pi.hThread)) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        throw WinError("ResumeThread()");
    }

    out.hWrite = INVALID_HANDLE_VALUE;
    CloseHandle(pi.hThread);
    pid.set(pi.hProcess, pi.dwProcessId);
#endif

    std::thread writerThread;

    std::promise<void> promise;

    Finally doJoin([&]() {
        if (writerThread.joinable())
            writerThread.join();
    });


    if (source) {
#ifndef _WIN32
        in.readSide = -1;
#else
        in.hRead = INVALID_HANDLE_VALUE;
#endif
        writerThread = std::thread([&]() {
            try {
                std::vector<unsigned char> buf(8 * 1024);
                while (true) {
                    size_t n;
                    try {
                        n = source->read(buf.data(), buf.size());
                    } catch (EndOfFile &) {
                        break;
                    }
#ifndef _WIN32
                    writeFull(in.writeSide.get(), buf.data(), n);
#else
                    writeFull(in.hWrite.get(), buf.data(), n);
#endif
                }
                promise.set_value();
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
#ifndef _WIN32
            in.writeSide = -1;
#else
            in.hWrite = INVALID_HANDLE_VALUE;
#endif
        });
    }
#ifndef _WIN32
    if (options.standardOut)
        drainFD(out.readSide.get(), *options.standardOut);
#else
    if (options.standardOut)
        drainFD(out.hRead.get(), *options.standardOut);
#endif

    /* Wait for the child to finish. */
    int status = pid.wait();

//    std::cerr << "_________________executable='" << to_bytes(pathW(executable)) << "' status=" << status << std::endl;

    /* Wait for the writer thread to finish. */
    if (source) promise.get_future().get();

//    std::cerr << "_________________executable='" << to_bytes(pathW(executable)) << "' got future" << std::endl;

    if (status)
        throw ExecError(status, fmt("program '%1%' %2%", options.program, statusToString(status)));
}

#ifndef _WIN32
void closeMostFDs(const set<int> & exceptions)
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
        throw PosixError("setting close-on-exec flag");
}
#endif

//////////////////////////////////////////////////////////////////////


bool _isInterrupted = false;

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
    if (!interruptThrown && !std::uncaught_exception()) {
        interruptThrown = true;
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
#ifndef _WIN32
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
#else
    if (status != 0)
        return (format("with exit code %1%") % status).str();
    else
        return "succeeded";
#endif
}


bool statusOk(int status)
{
#ifndef _WIN32
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    return status == 0;
#endif
}


bool hasPrefix(const string & s, const string & prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}


bool hasSuffix(const string & s, const string & suffix)
{
    return s.size() >= suffix.size() && string(s, s.size() - suffix.size()) == suffix;
}


std::string toLower(const std::string & s)
{
    std::string r(s);
    for (auto & c : r)
        c = std::tolower(c);
    return r;
}


std::string shellEscape(const std::string & s)
{
    std::string r = "'";
    for (auto & i : s)
        if (i == '\'') r += "'\\''"; else r += i;
    r += '\'';
    return r;
}

#ifdef _WIN32
//   https://docs.microsoft.com/en-us/windows/desktop/api/shellapi/nf-shellapi-commandlinetoargvw
//   https://docs.microsoft.com/en-us/previous-versions//17w5ykft(v=vs.85)
std::string windowsEscape(const std::string & s)
{
    if (s.empty() || s.find_first_of("&()<>[]{}^=;!'+,`!\"\t ") != std::string::npos) {
        std::string r = "\"";
        for (auto i=0; i < s.length(); ) {
            if (s[i] == '"') {
                r += '\\';
                r += s[i++];
            } else if (s[i] == '\\') {
                for (int j = 1; ; j++) {
                    if (i+j == s.length()) {
                        while (j--) { r += '\\'; r += '\\'; i++; };
                        break;
                    } else if (s[i+j] == '"') {
                        while (j--) { r += '\\'; r += '\\'; i++; }
                        r += L'\\';
                        r += s[i++];
                        break;
                    } else if (s[i+j] != '\\') {
                        while (j--) { r += '\\'; i++; };
                        r += s[i++];
                        break;
                    }
                }
            } else {
                r += s[i++];
            }
        }
        r += '"';
        return r;
    } else
        return s;
}

std::wstring windowsEscapeW(const std::wstring & s)
{
    if (s.empty() || s.find_first_of(L"&()<>[]{}^=;!'+,`!\"\t ") != std::wstring::npos) {
        std::wstring r = L"\"";
        for (auto i=0; i < s.length(); ) {
            if (s[i] == L'"') {
                r += L'\\';
                r += s[i++];
            } else if (s[i] == L'\\') {
                for (int j = 1; ; j++) {
                    if (i+j == s.length()) {
                        while (j--) { r += L'\\'; r += L'\\'; i++; };
                        break;
                    } else if (s[i+j] == L'"') {
                        while (j--) { r += L'\\'; r += L'\\'; i++; }
                        r += L'\\';
                        r += s[i++];
                        break;
                    } else if (s[i+j] != L'\\') {
                        while (j--) { r += L'\\'; i++; };
                        r += s[i++];
                        break;
                    }
                }
            } else {
                r += s[i++];
            }
        }
        r += L'"';
        return r;
    } else
        return s;
}
#endif

void ignoreException()
{
    try {
        throw;
    } catch (std::exception & e) {
        printError(format("error (ignored): %1%") % e.what());
    }
}


std::string filterANSIEscapes(const std::string & s, bool filterAll, unsigned int width)
{
    std::string t, e;
    size_t w = 0;
    auto i = s.begin();

    while (w < (size_t) width && i != s.end()) {

        if (*i == '\x1B') {
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

        else if (*i == '\r')
            // do nothing for now
            i++;

        else {
            t += *i++; w++;
        }
    }

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


void callFailure(const std::function<void(std::exception_ptr exc)> & failure, std::exception_ptr exc)
{
    try {
        failure(exc);
    } catch (std::exception & e) {
        printError(format("uncaught exception: %s") % e.what());
        abort();
    }
}


static Sync<std::pair<unsigned short, unsigned short>> windowSize{{0, 0}};

#ifndef _WIN32
static void updateWindowSize()
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize.lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
}


std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize.lock();
}


static Sync<std::list<std::function<void()>>> _interruptCallbacks;

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
        auto interruptCallbacks(_interruptCallbacks.lock());
        for (auto & callback : *interruptCallbacks) {
            try {
                callback();
            } catch (...) {
                ignoreException();
            }
        }
    }
}

static sigset_t savedSignalMask;

void startSignalHandlerThread()
{
    updateWindowSize();

    if (sigprocmask(SIG_BLOCK, nullptr, &savedSignalMask))
        throw PosixError("quering signal mask");

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGWINCH);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr))
        throw PosixError("blocking signals");

    std::thread(signalHandlerThread, set).detach();
}

void restoreSignals()
{
    if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
        throw PosixError("restoring signals");
}

/* RAII helper to automatically deregister a callback. */
struct InterruptCallbackImpl : InterruptCallback
{
    std::list<std::function<void()>>::iterator it;
    ~InterruptCallbackImpl() override
    {
        _interruptCallbacks.lock()->erase(it);
    }
};

std::unique_ptr<InterruptCallback> createInterruptCallback(std::function<void()> callback)
{
    auto interruptCallbacks(_interruptCallbacks.lock());
    interruptCallbacks->push_back(callback);

    auto res = std::make_unique<InterruptCallbackImpl>();
    res->it = interruptCallbacks->end();
    res->it--;

    return std::unique_ptr<InterruptCallback>(res.release());
}
#endif
}
