#include "serialise.hh"
#include "util.hh"

#include <cstring>
#include <cerrno>
#include <memory>

#include <boost/coroutine2/coroutine.hpp>


namespace nix {


void BufferedSink::operator () (const unsigned char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new unsigned char[bufSize]);

    while (len) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (bufPos + len >= bufSize) {
            flush();
            write(data, len);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + len > bufSize ? bufSize - bufPos : len;
        memcpy(buffer.get() + bufPos, data, n);
        data += n; bufPos += n; len -= n;
        if (bufPos == bufSize) flush();
    }
}


void BufferedSink::flush()
{
    if (bufPos == 0) return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    write(buffer.get(), n);
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreException(); }
}


size_t threshold = 256 * 1024 * 1024;

static void warnLargeDump()
{
    printError("warning: dumping very large path (> 256 MiB); this may run out of memory");
}


void FdSink::write(const unsigned char * data, size_t len)
{
    written += len;
    static bool warned = false;
    if (warn && !warned) {
        if (written > threshold) {
            warnLargeDump();
            warned = true;
        }
    }
    try {
        writeFull(fd, data, len);
    } catch (SysError & e) {
        _good = false;
        throw;
    }
}


bool FdSink::good()
{
    return _good;
}


void Source::operator () (unsigned char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n; len -= n;
    }
}


std::string Source::drain()
{
    std::string s;
    std::vector<unsigned char> buf(8192);
    while (true) {
        size_t n;
        try {
            n = read(buf.data(), buf.size());
            s.append((char *) buf.data(), n);
        } catch (EndOfFile &) {
            break;
        }
    }
    return s;
}


size_t BufferedSource::read(unsigned char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new unsigned char[bufSize]);

    if (!bufPosIn) bufPosIn = readUnbuffered(buffer.get(), bufSize);

    /* Copy out the data in the buffer. */
    size_t n = len > bufPosIn - bufPosOut ? bufPosIn - bufPosOut : len;
    memcpy(data, buffer.get() + bufPosOut, n);
    bufPosOut += n;
    if (bufPosIn == bufPosOut) bufPosIn = bufPosOut = 0;
    return n;
}


bool BufferedSource::hasData()
{
    return bufPosOut < bufPosIn;
}


size_t FdSource::readUnbuffered(unsigned char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, (char *) data, len);
    } while (n == -1 && errno == EINTR);
    if (n == -1) { _good = false; throw SysError("reading from file"); }
    if (n == 0) { _good = false; throw EndOfFile("unexpected end-of-file"); }
    read += n;
    return n;
}


bool FdSource::good()
{
    return _good;
}


size_t StringSource::read(unsigned char * data, size_t len)
{
    if (pos == s.size()) throw EndOfFile("end of string reached");
    size_t n = s.copy((char *) data, len, pos);
    pos += n;
    return n;
}


#if BOOST_VERSION >= 106300 && BOOST_VERSION < 106600
#error Coroutines are broken in this version of Boost!
#endif

std::unique_ptr<Source> sinkToSource(
    std::function<void(Sink &)> fun,
    std::function<void()> eof)
{
    struct SinkToSource : Source
    {
        typedef boost::coroutines2::coroutine<std::string> coro_t;

        std::function<void(Sink &)> fun;
        std::function<void()> eof;
        std::optional<coro_t::pull_type> coro;
        bool started = false;

        SinkToSource(std::function<void(Sink &)> fun, std::function<void()> eof)
            : fun(fun), eof(eof)
        {
        }

        std::string cur;
        size_t pos = 0;

        size_t read(unsigned char * data, size_t len) override
        {
            if (!coro)
                coro = coro_t::pull_type([&](coro_t::push_type & yield) {
                    LambdaSink sink([&](const unsigned char * data, size_t len) {
                            if (len) yield(std::string((const char *) data, len));
                        });
                    fun(sink);
                });

            if (!*coro) { eof(); abort(); }

            if (pos == cur.size()) {
                if (!cur.empty()) (*coro)();
                cur = coro->get();
                pos = 0;
            }

            auto n = std::min(cur.size() - pos, len);
            memcpy(data, (unsigned char *) cur.data() + pos, n);
            pos += n;

            return n;
        }
    };

    return std::make_unique<SinkToSource>(fun, eof);
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


void writeString(const unsigned char * buf, size_t len, Sink & sink)
{
    sink << len;
    sink(buf, len);
    writePadding(len, sink);
}


Sink & operator << (Sink & sink, const string & s)
{
    writeString((const unsigned char *) s.data(), s.size(), sink);
    return sink;
}


template<class T> void writeStrings(const T & ss, Sink & sink)
{
    sink << ss.size();
    for (auto & i : ss)
        sink << i;
}

Sink & operator << (Sink & sink, const Strings & s)
{
    writeStrings(s, sink);
    return sink;
}

Sink & operator << (Sink & sink, const StringSet & s)
{
    writeStrings(s, sink);
    return sink;
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


size_t readString(unsigned char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}


string readString(Source & source, size_t max)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    std::string res(len, 0);
    source((unsigned char*) res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator >> (Source & in, string & s)
{
    s = readString(in);
    return in;
}


template<class T> T readStrings(Source & source)
{
    auto count = readNum<size_t>(source);
    T ss;
    while (count--)
        ss.insert(ss.end(), readString(source));
    return ss;
}

template Paths readStrings(Source & source);
template PathSet readStrings(Source & source);


void StringSink::operator () (const unsigned char * data, size_t len)
{
    static bool warned = false;
    if (!warned && s->size() > threshold) {
        warnLargeDump();
        warned = true;
    }
    s->append((const char *) data, len);
}


}
