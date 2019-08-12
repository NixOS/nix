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
       return the number of bytes stored.  It blocks until at least
       one byte is available. */
    virtual size_t read(unsigned char * data, size_t len) = 0;

    virtual bool good() { return true; }

    std::string drain();
};


/* A buffered abstract source. */
struct BufferedSource : Source
{
    size_t bufSize, bufPosIn, bufPosOut;
    std::unique_ptr<unsigned char[]> buffer;

    BufferedSource(size_t bufSize = 32 * 1024)
        : bufSize(bufSize), bufPosIn(0), bufPosOut(0), buffer(nullptr) { }

    size_t read(unsigned char * data, size_t len) override;


    bool hasData();

protected:
    /* Underlying read call, to be overridden. */
    virtual size_t readUnbuffered(unsigned char * data, size_t len) = 0;
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

    FdSink& operator=(FdSink && s)
    {
        flush();
        fd = s.fd;
        s.fd = -1;
        warn = s.warn;
        written = s.written;
        return *this;
    }

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
    FdSource(FdSource&&) = default;

    FdSource& operator=(FdSource && s)
    {
        fd = s.fd;
        s.fd = -1;
        read = s.read;
        return *this;
    }

    bool good() override;
protected:
    size_t readUnbuffered(unsigned char * data, size_t len) override;
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

/* A reader that consumes the original Source until 'size'. */
struct SizedSource : Source
{
    Source & orig;
    size_t remain;
    SizedSource(Source & orig, size_t size)
        : orig(orig), remain(size) { }
    size_t read(unsigned char * data, size_t len)
    {
        if (this->remain <= 0) {
            throw EndOfFile("sized: unexpected end-of-file");
        }
        len = std::min(len, this->remain);
        size_t n = this->orig.read(data, len);
        this->remain -= n;
        return n;
    }

    /* Consume the original source until no remain data is left to consume. */
    size_t drainAll()
    {
        std::vector<unsigned char> buf(8192);
        size_t sum = 0;
        while (this->remain > 0) {
            size_t n = read(buf.data(), buf.size());
            sum += n;
        }
        return sum;
    }
};

/* Convert a function into a sink. */
struct LambdaSink : Sink
{
    typedef std::function<void(const unsigned char *, size_t)> lambda_t;

    lambda_t lambda;

    LambdaSink(const lambda_t & lambda) : lambda(lambda) { }

    virtual void operator () (const unsigned char * data, size_t len)
    {
        lambda(data, len);
    }
};


/* Convert a function into a source. */
struct LambdaSource : Source
{
    typedef std::function<size_t(unsigned char *, size_t)> lambda_t;

    lambda_t lambda;

    LambdaSource(const lambda_t & lambda) : lambda(lambda) { }

    size_t read(unsigned char * data, size_t len) override
    {
        return lambda(data, len);
    }
};


/* Convert a function that feeds data into a Sink into a Source. The
   Source executes the function as a coroutine. */
std::unique_ptr<Source> sinkToSource(
    std::function<void(Sink &)> fun,
    std::function<void()> eof = []() {
        throw EndOfFile("coroutine has finished");
    });


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
    buf[7] = (unsigned char) (n >> 56) & 0xff;
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

    return (T) n;
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
string readString(Source & source, size_t max = std::numeric_limits<size_t>::max());
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
