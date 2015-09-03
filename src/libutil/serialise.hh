#pragma once

#include "types.hh"
#include "util.hh"


namespace nix {


/* Abstract destination of binary data. */
struct Sink
{
    virtual ~Sink() { }
    virtual void operator () (const unsigned char * data, size_t len) = 0;
};


/* A buffered abstract sink. */
struct BufferedSink : Sink
{
    size_t bufSize, bufPos;
    unsigned char * buffer;

    BufferedSink(size_t bufSize = 32 * 1024)
        : bufSize(bufSize), bufPos(0), buffer(0) { }
    ~BufferedSink();

    void operator () (const unsigned char * data, size_t len);

    void flush();

    virtual void write(const unsigned char * data, size_t len) = 0;
};


/* Abstract source of binary data. */
struct Source
{
    virtual ~Source() { }

    /* Store exactly ‘len’ bytes in the buffer pointed to by ‘data’.
       It blocks until all the requested data is available, or throws
       an error if it is not going to be available.   */
    void operator () (unsigned char * data, size_t len);

    /* Store up to ‘len’ in the buffer pointed to by ‘data’, and
       return the number of bytes stored.  If blocks until at least
       one byte is available. */
    virtual size_t read(unsigned char * data, size_t len) = 0;
};


/* A buffered abstract source. */
struct BufferedSource : Source
{
    size_t bufSize, bufPosIn, bufPosOut;
    unsigned char * buffer;

    BufferedSource(size_t bufSize = 32 * 1024)
        : bufSize(bufSize), bufPosIn(0), bufPosOut(0), buffer(0) { }
    ~BufferedSource();

    size_t read(unsigned char * data, size_t len);

    /* Underlying read call, to be overridden. */
    virtual size_t readUnbuffered(unsigned char * data, size_t len) = 0;

    bool hasData();
};


/* A sink that writes data to a file descriptor. */
struct FdSink : BufferedSink
{
    int fd;
    bool warn;
    size_t written;

    FdSink() : fd(-1), warn(false), written(0) { }
    FdSink(int fd) : fd(fd), warn(false), written(0) { }
    ~FdSink();

    void write(const unsigned char * data, size_t len);
};


/* A source that reads data from a file descriptor. */
struct FdSource : BufferedSource
{
    int fd;
    FdSource() : fd(-1) { }
    FdSource(int fd) : fd(fd) { }
    size_t readUnbuffered(unsigned char * data, size_t len);
};


/* A sink that writes data to a string. */
struct StringSink : Sink
{
    string s;
    void operator () (const unsigned char * data, size_t len);
};


/* A source that reads data from a string. */
struct StringSource : Source
{
    const string & s;
    size_t pos;
    StringSource(const string & _s) : s(_s), pos(0) { }
    size_t read(unsigned char * data, size_t len);
};


void writePadding(size_t len, Sink & sink);
void writeString(const unsigned char * buf, size_t len, Sink & sink);

inline Sink & operator << (Sink & sink, uint64_t n)
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
    return sink;
}

Sink & operator << (Sink & sink, const string & s);
Sink & operator << (Sink & sink, const Strings & s);
Sink & operator << (Sink & sink, const StringSet & s);


void readPadding(size_t len, Source & source);
unsigned int readInt(Source & source);
unsigned long long readLongLong(Source & source);
size_t readString(unsigned char * buf, size_t max, Source & source);
string readString(Source & source);
template<class T> T readStrings(Source & source);

Source & operator >> (Source & in, string & s);
Source & operator >> (Source & in, unsigned int & n);


MakeError(SerialisationError, Error)


}
