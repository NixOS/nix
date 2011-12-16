#include "serialise.hh"
#include "util.hh"

#include <cstring>
#include <cerrno>


namespace nix {


BufferedSink::~BufferedSink()
{
    /* We can't call flush() here, because C++ for some insane reason
       doesn't allow you to call virtual methods from a destructor. */
    assert(!bufPos);
    if (buffer) delete[] buffer;
}

    
void BufferedSink::operator () (const unsigned char * data, size_t len)
{
    if (!buffer) buffer = new unsigned char[bufSize];
    
    while (len) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size and there is no unflushed data. */
        if (bufPos == 0 && len >= bufSize) {
            write(data, len);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + len > bufSize ? bufSize - bufPos : len;
        memcpy(buffer + bufPos, data, n);
        data += n; bufPos += n; len -= n;
        if (bufPos == bufSize) flush();
    }
}


void BufferedSink::flush()
{
    if (bufPos == 0) return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    write(buffer, n);
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreException(); }
}


void FdSink::write(const unsigned char * data, size_t len)
{
    writeFull(fd, data, len);
}


BufferedSource::~BufferedSource()
{
    if (buffer) delete[] buffer;
}


void BufferedSource::operator () (unsigned char * data, size_t len)
{
    if (!buffer) buffer = new unsigned char[bufSize];

    while (len) {
        if (!bufPosIn) bufPosIn = read(buffer, bufSize);
            
        /* Copy out the data in the buffer. */
        size_t n = len > bufPosIn - bufPosOut ? bufPosIn - bufPosOut : len;
        memcpy(data, buffer + bufPosOut, n);
        data += n; bufPosOut += n; len -= n;
        if (bufPosIn == bufPosOut) bufPosIn = bufPosOut = 0;
    }
}


size_t FdSource::read(unsigned char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, (char *) data, bufSize);
    } while (n == -1 && errno == EINTR);
    if (n == -1) throw SysError("reading from file");
    if (n == 0) throw EndOfFile("unexpected end-of-file");
    return n;
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


void writeInt(unsigned int n, Sink & sink)
{
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    sink(buf, sizeof(buf));
}


void writeLongLong(unsigned long long n, Sink & sink)
{
    unsigned char buf[8];
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    buf[4] = (n >> 32) & 0xff;
    buf[5] = (n >> 40) & 0xff;
    buf[6] = (n >> 48) & 0xff;
    buf[7] = (n >> 56) & 0xff;
    sink(buf, sizeof(buf));
}


void writeString(const string & s, Sink & sink)
{
    size_t len = s.length();
    writeInt(len, sink);
    sink((const unsigned char *) s.c_str(), len);
    writePadding(len, sink);
}


void writeStringSet(const StringSet & ss, Sink & sink)
{
    writeInt(ss.size(), sink);
    for (StringSet::iterator i = ss.begin(); i != ss.end(); ++i)
        writeString(*i, sink);
}


void readPadding(size_t len, Source & source)
{
    if (len % 8) {
        unsigned char zero[8];
        size_t n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


unsigned int readInt(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw SerialisationError("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}


unsigned long long readLongLong(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    return
        ((unsigned long long) buf[0]) |
        ((unsigned long long) buf[1] << 8) |
        ((unsigned long long) buf[2] << 16) |
        ((unsigned long long) buf[3] << 24) |
        ((unsigned long long) buf[4] << 32) |
        ((unsigned long long) buf[5] << 40) |
        ((unsigned long long) buf[6] << 48) |
        ((unsigned long long) buf[7] << 56);
}


string readString(Source & source)
{
    size_t len = readInt(source);
    unsigned char * buf = new unsigned char[len];
    AutoDeleteArray<unsigned char> d(buf);
    source(buf, len);
    readPadding(len, source);
    return string((char *) buf, len);
}

 
StringSet readStringSet(Source & source)
{
    unsigned int count = readInt(source);
    StringSet ss;
    while (count--)
        ss.insert(readString(source));
    return ss;
}


}
