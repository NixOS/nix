#include "nix/util/serialise.hh"
#include "nix/util/compression.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include <cstring>
#include <cerrno>
#include <memory>

#include <boost/coroutine2/coroutine.hpp>

#ifdef _WIN32
#  include <fileapi.h>
#  include <winsock2.h>
#  include "nix/util/windows-error.hh"
#else
#  include <poll.h>
#endif

namespace nix {

void BufferedSink::operator()(std::string_view data)
{
    if (!buffer)
        buffer = decltype(buffer)(new char[bufSize]);

    while (!data.empty()) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (bufPos + data.size() >= bufSize) {
            flush();
            writeUnbuffered(data);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + data.size() > bufSize ? bufSize - bufPos : data.size();
        memcpy(buffer.get() + bufPos, data.data(), n);
        data.remove_prefix(n);
        bufPos += n;
        if (bufPos == bufSize)
            flush();
    }
}

void BufferedSink::flush()
{
    if (bufPos == 0)
        return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    writeUnbuffered({buffer.get(), n});
}

FdSink::~FdSink()
{
    try {
        flush();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void FdSink::writeUnbuffered(std::string_view data)
{
    written += data.size();
    try {
        writeFull(fd, data);
    } catch (SystemError & e) {
        _good = false;
        throw;
    }
}

bool FdSink::good()
{
    return _good;
}

void Source::operator()(char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n;
        len -= n;
    }
}

void Source::operator()(std::string_view data)
{
    (*this)((char *) data.data(), data.size());
}

void Source::drainInto(Sink & sink)
{
    std::array<char, 8192> buf;
    while (true) {
        try {
            auto n = read(buf.data(), buf.size());
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

void Source::skip(size_t len)
{
    std::array<char, 8192> buf;
    while (len) {
        auto n = read(buf.data(), std::min(len, buf.size()));
        assert(n <= len);
        len -= n;
    }
}

size_t BufferedSource::read(char * data, size_t len)
{
    if (!buffer)
        buffer = decltype(buffer)(new char[bufSize]);

    if (!bufPosIn)
        bufPosIn = readUnbuffered(buffer.get(), bufSize);

    /* Copy out the data in the buffer. */
    auto n = std::min(len, bufPosIn - bufPosOut);
    memcpy(data, buffer.get() + bufPosOut, n);
    bufPosOut += n;
    if (bufPosIn == bufPosOut)
        bufPosIn = bufPosOut = 0;
    return n;
}

bool BufferedSource::hasData()
{
    return bufPosOut < bufPosIn;
}

size_t FdSource::readUnbuffered(char * data, size_t len)
{
#ifdef _WIN32
    DWORD n;
    checkInterrupt();
    if (!::ReadFile(fd, data, len, &n, NULL)) {
        _good = false;
        throw windows::WinError("ReadFile when FdSource::readUnbuffered");
    }
#else
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, data, len);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        _good = false;
        throw SysError("reading from file");
    }
    if (n == 0) {
        _good = false;
        throw EndOfFile(std::string(*endOfFileError));
    }
#endif
    read += n;
    return n;
}

bool FdSource::good()
{
    return _good;
}

bool FdSource::hasData()
{
    if (BufferedSource::hasData())
        return true;

    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        int fd_ = fromDescriptorReadOnly(fd);
        FD_SET(fd_, &fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        auto n = select(fd_ + 1, &fds, nullptr, nullptr, &timeout);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw SysError("polling file descriptor");
        }
        return FD_ISSET(fd, &fds);
    }
}

void FdSource::skip(size_t len)
{
    /* Discard data in the buffer. */
    if (len && buffer && bufPosIn - bufPosOut) {
        if (len >= bufPosIn - bufPosOut) {
            len -= bufPosIn - bufPosOut;
            bufPosIn = bufPosOut = 0;
        } else {
            bufPosOut += len;
            len = 0;
        }
    }

#ifndef _WIN32
    /* If we can, seek forward in the file to skip the rest. */
    if (isSeekable && len) {
        if (lseek(fd, len, SEEK_CUR) == -1) {
            if (errno == ESPIPE)
                isSeekable = false;
            else
                throw SysError("seeking forward in file");
        } else {
            read += len;
            return;
        }
    }
#endif

    /* Otherwise, skip by reading. */
    if (len)
        BufferedSource::skip(len);
}

size_t StringSource::read(char * data, size_t len)
{
    if (pos == s.size())
        throw EndOfFile("end of string reached");
    size_t n = s.copy(data, len, pos);
    pos += n;
    return n;
}

void StringSource::skip(size_t len)
{
    const size_t remain = s.size() - pos;
    if (len > remain) {
        pos = s.size();
        throw EndOfFile("end of string reached");
    }
    pos += len;
}

CompressedSource::CompressedSource(RestartableSource & source, const std::string & compressionMethod)
    : compressedData([&]() {
        StringSink sink;
        auto compressionSink = makeCompressionSink(compressionMethod, sink);
        source.drainInto(*compressionSink);
        compressionSink->finish();
        return std::move(sink.s);
    }())
    , compressionMethod(compressionMethod)
    , stringSource(compressedData)
{
}

std::unique_ptr<FinishSink> sourceToSink(std::function<void(Source &)> fun)
{
    struct SourceToSink : FinishSink
    {
        typedef boost::coroutines2::coroutine<bool> coro_t;

        std::function<void(Source &)> fun;
        std::optional<coro_t::push_type> coro;

        SourceToSink(std::function<void(Source &)> fun)
            : fun(fun)
        {
        }

        std::string_view cur;

        void operator()(std::string_view in) override
        {
            if (in.empty())
                return;
            cur = in;

            if (!coro) {
                coro = coro_t::push_type([&](coro_t::pull_type & yield) {
                    LambdaSource source([&](char * out, size_t out_len) {
                        if (cur.empty()) {
                            yield();
                            if (yield.get())
                                throw EndOfFile("coroutine has finished");
                        }

                        size_t n = cur.copy(out, out_len);
                        cur.remove_prefix(n);
                        return n;
                    });
                    fun(source);
                });
            }

            if (!*coro) {
                unreachable();
            }

            if (!cur.empty()) {
                (*coro)(false);
            }
        }

        void finish() override
        {
            if (coro && *coro)
                (*coro)(true);
        }
    };

    return std::make_unique<SourceToSink>(fun);
}

std::unique_ptr<Source> sinkToSource(std::function<void(Sink &)> fun, std::function<void()> eof)
{
    struct SinkToSource : Source
    {
        typedef boost::coroutines2::coroutine<std::string_view> coro_t;

        std::function<void(Sink &)> fun;
        std::function<void()> eof;
        std::optional<coro_t::pull_type> coro;

        SinkToSource(std::function<void(Sink &)> fun, std::function<void()> eof)
            : fun(fun)
            , eof(eof)
        {
        }

        std::string_view cur;

        size_t read(char * data, size_t len) override
        {
            bool hasCoro = coro.has_value();
            if (!hasCoro) {
                coro = coro_t::pull_type([&](coro_t::push_type & yield) {
                    LambdaSink sink([&](std::string_view data) {
                        if (!data.empty()) {
                            yield(data);
                        }
                    });
                    fun(sink);
                });
            }

            if (cur.empty()) {
                if (hasCoro) {
                    (*coro)();
                }
                if (*coro) {
                    cur = coro->get();
                } else {
                    coro.reset();
                    eof();
                    unreachable();
                }
            }

            size_t n = cur.copy(data, len);
            cur.remove_prefix(n);

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

Sink & operator<<(Sink & sink, std::string_view s)
{
    writeString(s, sink);
    return sink;
}

template<class T>
void writeStrings(const T & ss, Sink & sink)
{
    sink << ss.size();
    for (auto & i : ss)
        sink << i;
}

Sink & operator<<(Sink & sink, const Strings & s)
{
    writeStrings(s, sink);
    return sink;
}

Sink & operator<<(Sink & sink, const StringSet & s)
{
    writeStrings(s, sink);
    return sink;
}

Sink & operator<<(Sink & sink, const Error & ex)
{
    auto & info = ex.info();
    sink << "Error" << info.level << "Error" // removed
         << info.msg.str() << 0              // FIXME: info.errPos
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
            if (zero[i])
                throw SerialisationError("non-zero padding");
    }
}

size_t readString(char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max)
        throw SerialisationError("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}

std::string readString(Source & source, size_t max)
{
    auto len = readNum<size_t>(source);
    if (len > max)
        throw SerialisationError("string is too long");
    std::string res(len, 0);
    source(res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator>>(Source & in, std::string & s)
{
    s = readString(in);
    return in;
}

template<class T>
T readStrings(Source & source)
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
    [[maybe_unused]] auto name = readString(source); // removed
    auto msg = readString(source);
    ErrorInfo info{
        .level = level,
        .msg = HintFmt(msg),
    };
    auto havePos = readNum<size_t>(source);
    assert(havePos == 0);
    auto nrTraces = readNum<size_t>(source);
    for (size_t i = 0; i < nrTraces; ++i) {
        havePos = readNum<size_t>(source);
        assert(havePos == 0);
        info.traces.push_back(Trace{.hint = HintFmt(readString(source))});
    }
    return Error(std::move(info));
}

void StringSink::operator()(std::string_view data)
{
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

std::unique_ptr<RestartableSource> restartableSourceFromFactory(std::function<std::unique_ptr<Source>()> factory)
{
    struct RestartableSourceImpl : RestartableSource
    {
        RestartableSourceImpl(decltype(factory) factory_)
            : factory_(std::move(factory_))
            , impl(this->factory_())
        {
        }

        decltype(factory) factory_;
        std::unique_ptr<Source> impl = factory_();

        size_t read(char * data, size_t len) override
        {
            return impl->read(data, len);
        }

        bool good() override
        {
            return impl->good();
        }

        void skip(size_t len) override
        {
            return impl->skip(len);
        }

        void restart() override
        {
            impl = factory_();
        }
    };

    return std::make_unique<RestartableSourceImpl>(std::move(factory));
}

} // namespace nix
