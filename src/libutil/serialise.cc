#include "serialise.hh"
#include "util.hh"

#include <cstring>
#include <cerrno>
#include <memory>

#include <boost/coroutine2/coroutine.hpp>


namespace nix {


void BufferedSink::operator () (std::string_view data)
{
    if (!buffer) buffer = decltype(buffer)(new char[bufSize]);

    while (!data.empty()) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (bufPos + data.size() >= bufSize) {
            flush();
            write(data);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + data.size() > bufSize ? bufSize - bufPos : data.size();
        memcpy(buffer.get() + bufPos, data.data(), n);
        data.remove_prefix(n); bufPos += n;
        if (bufPos == bufSize) flush();
    }
}


void BufferedSink::flush()
{
    if (bufPos == 0) return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    write({buffer.get(), n});
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreException(); }
}


size_t threshold = 256 * 1024 * 1024;

static void warnLargeDump()
{
    warn("dumping very large path (> 256 MiB); this may run out of memory");
}


void FdSink::write(std::string_view data)
{
    written += data.size();
    static bool warned = false;
    if (warn && !warned) {
        if (written > threshold) {
            warnLargeDump();
            warned = true;
        }
    }
    try {
        writeFull(fd, data);
    } catch (SysError & e) {
        _good = false;
        throw;
    }
}


bool FdSink::good()
{
    return _good;
}


void Source::operator () (char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n; len -= n;
    }
}


void Source::drainInto(Sink & sink)
{
    std::string s;
    std::vector<char> buf(8192);
    while (true) {
        size_t n;
        try {
            n = read(buf.data(), buf.size());
            sink({buf.data(), n});
        } catch (EndOfFile &) {
            break;
        }
    }
}


std::string Source::drain()
{
    StringSink s;
    drainInto(s);
    return std::move(s.s);
}


size_t BufferedSource::read(char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new char[bufSize]);

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


size_t FdSource::readUnbuffered(char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, data, len);
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


size_t StringSource::read(char * data, size_t len)
{
    if (pos == s.size()) throw EndOfFile("end of string reached");
    size_t n = s.copy(data, len, pos);
    pos += n;
    return n;
}


#if BOOST_VERSION >= 106300 && BOOST_VERSION < 106600
#error Coroutines are broken in this version of Boost!
#endif

/* A concrete datatype allow virtual dispatch of stack allocation methods. */
struct VirtualStackAllocator {
    StackAllocator *allocator = StackAllocator::defaultAllocator;

    boost::context::stack_context allocate() {
        return allocator->allocate();
    }

    void deallocate(boost::context::stack_context sctx) {
        allocator->deallocate(sctx);
    }
};


/* This class reifies the default boost coroutine stack allocation strategy with
   a virtual interface. */
class DefaultStackAllocator : public StackAllocator {
    boost::coroutines2::default_stack stack;

    boost::context::stack_context allocate() {
        return stack.allocate();
    }

    void deallocate(boost::context::stack_context sctx) {
        stack.deallocate(sctx);
    }
};

static DefaultStackAllocator defaultAllocatorSingleton;

StackAllocator *StackAllocator::defaultAllocator = &defaultAllocatorSingleton;


std::unique_ptr<FinishSink> sourceToSink(std::function<void(Source &)> fun)
{
    struct SourceToSink : FinishSink
    {
        typedef boost::coroutines2::coroutine<bool> coro_t;

        std::function<void(Source &)> fun;
        std::optional<coro_t::push_type> coro;

        SourceToSink(std::function<void(Source &)> fun) : fun(fun)
        {
        }

        std::string_view cur;

        void operator () (std::string_view in) override
        {
            if (in.empty()) return;
            cur = in;

            if (!coro)
                coro = coro_t::push_type(VirtualStackAllocator{}, [&](coro_t::pull_type & yield) {
                    LambdaSource source([&](char *out, size_t out_len) {
                        if (cur.empty()) {
                            yield();
                            if (yield.get()) {
                                return (size_t)0;
                            }
                        }

                        size_t n = std::min(cur.size(), out_len);
                        memcpy(out, cur.data(), n);
                        cur.remove_prefix(n);
                        return n;
                    });
                    fun(source);
                });

            if (!*coro) { abort(); }

            if (!cur.empty()) (*coro)(false);
        }

        void finish() override
        {
            if (!coro) return;
            if (!*coro) abort();
            (*coro)(true);
            if (*coro) abort();
        }
    };

    return std::make_unique<SourceToSink>(fun);
}


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

        SinkToSource(std::function<void(Sink &)> fun, std::function<void()> eof)
            : fun(fun), eof(eof)
        {
        }

        std::string cur;
        size_t pos = 0;

        size_t read(char * data, size_t len) override
        {
            if (!coro)
                coro = coro_t::pull_type(VirtualStackAllocator{}, [&](coro_t::push_type & yield) {
                    LambdaSink sink([&](std::string_view data) {
                        if (!data.empty()) yield(std::string(data));
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
            memcpy(data, cur.data() + pos, n);
            pos += n;

            return n;
        }
    };

    return std::make_unique<SinkToSource>(fun, eof);
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        char zero[8];
        memset(zero, 0, sizeof(zero));
        sink({zero, 8 - (len % 8)});
    }
}


void writeString(std::string_view data, Sink & sink)
{
    sink << data.size();
    sink(data);
    writePadding(data.size(), sink);
}


Sink & operator << (Sink & sink, std::string_view s)
{
    writeString(s, sink);
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

Sink & operator << (Sink & sink, const Error & ex)
{
    auto info = ex.info();
    sink
        << "Error"
        << info.level
        << "Error" // removed
        << info.msg.str()
        << 0 // FIXME: info.errPos
        << info.traces.size();
    for (auto & trace : info.traces) {
        sink << 0; // FIXME: trace.pos
        sink << trace.hint.str();
    }
    return sink;
}


void readPadding(size_t len, Source & source)
{
    if (len % 8) {
        char zero[8];
        size_t n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


size_t readString(char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}


std::string readString(Source & source, size_t max)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    std::string res(len, 0);
    source(res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator >> (Source & in, std::string & s)
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


Error readError(Source & source)
{
    auto type = readString(source);
    assert(type == "Error");
    auto level = (Verbosity) readInt(source);
    auto name = readString(source); // removed
    auto msg = readString(source);
    ErrorInfo info {
        .level = level,
        .msg = hintformat(std::move(format("%s") % msg)),
    };
    auto havePos = readNum<size_t>(source);
    assert(havePos == 0);
    auto nrTraces = readNum<size_t>(source);
    for (size_t i = 0; i < nrTraces; ++i) {
        havePos = readNum<size_t>(source);
        assert(havePos == 0);
        info.traces.push_back(Trace {
            .hint = hintformat(std::move(format("%s") % readString(source)))
        });
    }
    return Error(std::move(info));
}


void StringSink::operator () (std::string_view data)
{
    static bool warned = false;
    if (!warned && s.size() > threshold) {
        warnLargeDump();
        warned = true;
    }
    s.append(data);
}

size_t ChainSource::read(char * data, size_t len)
{
    if (useSecond) {
        return source2.read(data, len);
    } else {
        try {
            return source1.read(data, len);
        } catch (EndOfFile &) {
            useSecond = true;
            return this->read(data, len);
        }
    }
}

}
