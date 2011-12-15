#ifndef __SERIALISE_H
#define __SERIALISE_H

#include "types.hh"


namespace nix {


/* Abstract destination of binary data. */
struct Sink 
{
    virtual ~Sink() { }
    virtual void operator () (const unsigned char * data, unsigned int len) = 0;
};


/* Abstract source of binary data. */
struct Source
{
    virtual ~Source() { }
    
    /* The callee should store exactly *len bytes in the buffer
       pointed to by data.  It should block if that much data is not
       yet available, or throw an error if it is not going to be
       available. */
    virtual void operator () (unsigned char * data, unsigned int len) = 0;
};


/* A sink that writes data to a file descriptor (using a buffer). */
struct FdSink : Sink
{
    int fd;
    unsigned int bufSize, bufPos;
    unsigned char * buffer;

    FdSink() : fd(-1), bufSize(32 * 1024), bufPos(0), buffer(0) { }
    
    FdSink(int fd, unsigned int bufSize = 32 * 1024)
        : fd(fd), bufSize(bufSize), bufPos(0), buffer(0) { }

    ~FdSink()
    {
        flush();
        if (buffer) delete[] buffer;
    }
    
    void operator () (const unsigned char * data, unsigned int len);

    void flush();
};


/* A source that reads data from a file descriptor. */
struct FdSource : Source
{
    int fd;
    unsigned int bufSize, bufPosIn, bufPosOut;
    unsigned char * buffer;

    FdSource() : fd(-1), bufSize(32 * 1024), bufPosIn(0), bufPosOut(0), buffer(0) { }
    
    FdSource(int fd, unsigned int bufSize = 32 * 1024)
        : fd(fd), bufSize(bufSize), bufPosIn(0), bufPosOut(0), buffer(0) { }
    
    ~FdSource()
    {
        if (buffer) delete[] buffer;
    }
    
    void operator () (unsigned char * data, unsigned int len);
};


/* A sink that writes data to a string. */
struct StringSink : Sink
{
    string s;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        s.append((const char *) data, len);
    }
};


/* A source that reads data from a string. */
struct StringSource : Source
{
    const string & s;
    unsigned int pos;
    StringSource(const string & _s) : s(_s), pos(0) { }
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        s.copy((char *) data, len, pos);
        pos += len;
        if (pos > s.size())
            throw Error("end of string reached");
    }
};


void writePadding(unsigned int len, Sink & sink);
void writeInt(unsigned int n, Sink & sink);
void writeLongLong(unsigned long long n, Sink & sink);
void writeString(const string & s, Sink & sink);
void writeStringSet(const StringSet & ss, Sink & sink);

void readPadding(unsigned int len, Source & source);
unsigned int readInt(Source & source);
unsigned long long readLongLong(Source & source);
string readString(Source & source);
StringSet readStringSet(Source & source);


MakeError(SerialisationError, Error)


}


#endif /* !__SERIALISE_H */
