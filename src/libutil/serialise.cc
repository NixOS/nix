#include "serialise.hh"
#include "util.hh"


namespace nix {


void FdSink::operator () (const unsigned char * data, unsigned int len)
{
    writeFull(fd, data, len);
}


void FdSource::operator () (unsigned char * data, unsigned int len)
{
    readFull(fd, data, len);
}


void writePadding(unsigned int len, Sink & sink)
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


void writeString(const string & s, Sink & sink)
{
    unsigned int len = s.length();
    writeInt(len, sink);
    sink((const unsigned char *) s.c_str(), len);
    writePadding(len, sink);
}


void readPadding(unsigned int len, Source & source)
{
    if (len % 8) {
        unsigned char zero[8];
        unsigned int n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw Error("non-zero padding");
    }
}


unsigned int readInt(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw Error("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}


string readString(Source & source)
{
    unsigned int len = readInt(source);
    char buf[len];
    source((unsigned char *) buf, len);
    readPadding(len, source);
    return string(buf, len);
}

 
}
