#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

extern "C" {
#include "md5.h"
}

#include "hash.hh"


Hash::Hash()
{
    memset(hash, 0, sizeof(hash));
}


bool Hash::operator == (Hash & h2)
{
    for (unsigned int i = 0; i < hashSize; i++)
        if (hash[i] != h2.hash[i]) return false;
    return true;
}


bool Hash::operator != (Hash & h2)
{
    return !(*this == h2);
}


Hash::operator string() const
{
    ostringstream str;
    for (unsigned int i = 0; i < hashSize; i++) {
        str.fill('0');
        str.width(2);
        str << hex << (int) hash[i];
    }
    return str.str();
}

    
Hash parseHash(const string & s)
{
    Hash hash;
    if (s.length() != Hash::hashSize * 2)
        throw BadRefError("invalid hash: " + s);
    for (unsigned int i = 0; i < Hash::hashSize; i++) {
        string s2(s, i * 2, 2);
        if (!isxdigit(s2[0]) || !isxdigit(s2[1])) 
            throw BadRefError("invalid hash: " + s);
        istringstream str(s2);
        int n;
        str >> hex >> n;
        hash.hash[i] = n;
    }
    return hash;
}


bool isHash(const string & s)
{
    if (s.length() != 32) return false;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}


Hash hashString(const string & s)
{
    Hash hash;
    md5_buffer(s.c_str(), s.length(), hash.hash);
    return hash;
}


Hash hashFile(const string & fileName)
{
    Hash hash;
    FILE * file = fopen(fileName.c_str(), "rb");
    if (!file)
        throw SysError("file `" + fileName + "' does not exist");
    int err = md5_stream(file, hash.hash);
    fclose(file);
    if (err) throw SysError("cannot hash file " + fileName);
    return hash;
}


struct HashSink : DumpSink
{
    struct md5_ctx ctx;
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        md5_process_bytes(data, len, &ctx);
    }
};


Hash hashPath(const string & path)
{
    Hash hash;
    HashSink sink;
    md5_init_ctx(&sink.ctx);
    dumpPath(path, sink);
    md5_finish_ctx(&sink.ctx, hash.hash);
    return hash;
}


static void pad(unsigned int len, DumpSink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


static void writeInt(unsigned int n, DumpSink & sink)
{
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    sink(buf, sizeof(buf));
}


static void writeString(const string & s, DumpSink & sink)
{
    unsigned int len = s.length();
    writeInt(len, sink);
    sink((const unsigned char *) s.c_str(), len);
    pad(len, sink);
}


static void dumpEntries(const string & path, DumpSink & sink)
{
    DIR * dir = opendir(path.c_str());
    if (!dir) throw SysError("opening directory " + path);
    
    struct dirent * dirent;

    /* !!! sort entries */

    while (errno = 0, dirent = readdir(dir)) {
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        writeString("entry", sink);
        writeString("(", sink);
        writeString("name", sink);
        writeString(name, sink);
        writeString("file", sink);
        dumpPath(path + "/" + name, sink);
        writeString(")", sink);
    }

    if (errno) throw SysError("reading directory " + path);
    
    closedir(dir); /* !!! close on exception */
}


static void dumpContents(const string & path, unsigned int size, 
    DumpSink & sink)
{
    writeString("contents", sink);
    writeInt(size, sink);

    int fd = open(path.c_str(), O_RDONLY);
    if (!fd) throw SysError("opening file " + path);
    
    unsigned char buf[16384];

    unsigned int total = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf)))) {
        if (n == -1) throw SysError("reading file " + path);
        total += n;
        sink(buf, n);
    }

    if (total != size)
        throw SysError("file changed while reading it: " + path);

    pad(size, sink);

    close(fd); /* !!! close on exception */
}


void dumpPath(const string & path, DumpSink & sink)
{
    cerr << path << endl;

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError("getting attributes of path " + path);

    writeString("(", sink);

    if (S_ISREG(st.st_mode)) {
        writeString("type", sink);
        writeString("regular", sink);
        dumpContents(path, st.st_size, sink);
    } else if (S_ISDIR(st.st_mode)) {
        writeString("type", sink);
        writeString("directory", sink);
        dumpEntries(path, sink);
    } else throw Error("unknown file type: " + path);

    writeString(")", sink);
}
