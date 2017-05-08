#pragma once

#include <memory>

#include "types.hh"
#include "util.hh"


namespace nix {


/* Abstract destination of binary data. */
struct Sink
{
    virtual ~Sink() { }
    virtual void operator () (const unsigned char * data, size_t len) = 0;
    virtual bool good() { return true; }

    void operator () (const std::string & s)
    {
        (*this)((const unsigned char *) s.data(), s.size());
    }
};


/* A buffered abstract sink. */
struct BufferedSink : Sink
{
    size_t bufSize, bufPos;
    std::unique_ptr<unsigned char[]> buffer;

    BufferedSink(size_t bufSize = 32 * 1024)
        : bufSize(bufSize), bufPos(0), buffer(nullptr) { }

    void operator () (const unsigned char * data, size_t len) override;

    void operator () (const std::string & s)
    {
        Sink::operator()(s);
    }

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

    virtual bool good() { return true; }
};


/* A buffered abstract source. */
struct BufferedSource : Source
{
    size_t bufSize, bufPosIn, bufPosOut;
    std::unique_ptr<unsigned char[]> buffer;

    BufferedSource(size_t bufSize = 32 * 1024)
        : bufSize(bufSize), bufPosIn(0), bufPosOut(0), buffer(nullptr) { }

    size_t read(unsigned char * data, size_t len) override;

    /* Underlying read call, to be overridden. */
    virtual size_t readUnbuffered(unsigned char * data, size_t len) = 0;

    bool hasData();
};


/* A sink that writes data to a file descriptor. */
struct FdSink : BufferedSink
{
    int fd;
    bool warn = false;
    size_t written = 0;

    FdSink() : fd(-1) { }
    FdSink(int fd) : fd(fd) { }
    FdSink(FdSink&&) = default;
    FdSink& operator=(FdSink&&) = default;
    ~FdSink();

    void write(const unsigned char * data, size_t len) override;

    bool good() override;

private:
    bool _good = true;
};


/* A source that reads data from a file descriptor. */
struct FdSource : BufferedSource
{
    int fd;
    size_t read = 0;

    FdSource() : fd(-1) { }
    FdSource(int fd) : fd(fd) { }
    size_t readUnbuffered(unsigned char * data, size_t len) override;
    bool good() override;
private:
    bool _good = true;
};


/* A sink that writes data to a string. */
struct StringSink : Sink
{
    ref<std::string> s;
    StringSink() : s(make_ref<std::string>()) { };
    StringSink(ref<std::string> s) : s(s) { };
    void operator () (const unsigned char * data, size_t len) override;
};


/* A source that reads data from a string. */
struct StringSource : Source
{
    const string & s;
    size_t pos;
    StringSource(const string & _s) : s(_s), pos(0) { }
    size_t read(unsigned char * data, size_t len) override;
};


/* Adapter class of a Source that saves all data read to `s'. */
struct TeeSource : Source
{
    Source & orig;
    ref<std::string> data;
    TeeSource(Source & orig)
        : orig(orig), data(make_ref<std::string>()) { }
    size_t read(unsigned char * data, size_t len)
    {
        size_t n = orig.read(data, len);
        this->data->append((const char *) data, n);
        return n;
    }
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


MakeError(SerialisationError, Error)


template<typename T>
T readNum(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));

    uint64_t n =
        ((unsigned long long) buf[0]) |
        ((unsigned long long) buf[1] << 8) |
        ((unsigned long long) buf[2] << 16) |
        ((unsigned long long) buf[3] << 24) |
        ((unsigned long long) buf[4] << 32) |
        ((unsigned long long) buf[5] << 40) |
        ((unsigned long long) buf[6] << 48) |
        ((unsigned long long) buf[7] << 56);

    if (n > std::numeric_limits<T>::max())
        throw SerialisationError("serialised integer %d is too large for type '%s'", n, typeid(T).name());

    return n;
}


inline unsigned int readInt(Source & source)
{
    return readNum<unsigned int>(source);
}


inline uint64_t readLongLong(Source & source)
{
    return readNum<uint64_t>(source);
}


void readPadding(size_t len, Source & source);
size_t readString(unsigned char * buf, size_t max, Source & source);
string readString(Source & source);
template<class T> T readStrings(Source & source);

Source & operator >> (Source & in, string & s);

template<typename T>
Source & operator >> (Source & in, T & n)
{
    n = readNum<T>(in);
    return in;
}

template<typename T>
Source & operator >> (Source & in, bool & b)
{
    b = readNum<uint64_t>(in);
    return in;
}


}
