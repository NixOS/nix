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


/* A sink that writes data to a file descriptor. */
struct FdSink : Sink
{
    int fd;

    FdSink()
    {
        fd = 0;
    }
    
    FdSink(int fd) 
    {
        this->fd = fd;
    }
    
    void operator () (const unsigned char * data, unsigned int len);
};


/* A source that reads data from a file descriptor. */
struct FdSource : Source
{
    int fd;

    FdSource()
    {
        fd = 0;
    }
    
    FdSource(int fd) 
    {
        this->fd = fd;
    }
    
    void operator () (unsigned char * data, unsigned int len);
};


void writePadding(unsigned int len, Sink & sink);
void writeInt(unsigned int n, Sink & sink);
void writeString(const string & s, Sink & sink);
void writeStringSet(const StringSet & ss, Sink & sink);

void readPadding(unsigned int len, Source & source);
unsigned int readInt(Source & source);
string readString(Source & source);
StringSet readStringSet(Source & source);


}


#endif /* !__SERIALISE_H */
