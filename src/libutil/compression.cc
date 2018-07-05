#include "compression.hh"
#include "util.hh"
#include "finally.hh"
#include "logging.hh"

#include <lzma.h>
#include <bzlib.h>
#include <cstdio>
#include <cstring>

#if HAVE_BROTLI
#include <brotli/decode.h>
#include <brotli/encode.h>
#endif // HAVE_BROTLI

#if HAVE_ZSTD
#include <zstd.h>
// Not set by earlier versions, so be sure it's set
#ifndef ZSTD_CLEVEL_DEFAULT
#  define ZSTD_CLEVEL_DEFAULT 3
#endif
#endif

#include <algorithm>
#include <iostream>

namespace nix {

static const size_t bufSize = 32 * 1024;

static const int COMPRESSION_LEVEL_DEFAULT = -1;

static unsigned checkLevel(unsigned min, unsigned max, unsigned methodDefault, std::string method, int level) {
    if (level == COMPRESSION_LEVEL_DEFAULT)
        return methodDefault;
    if (level < 0)
        throw CompressionError("compression level must be a non-negative integer");

    unsigned l = static_cast<unsigned>(level);
    if (min <= l && l <= max)
        return l;

    throw CompressionError("requested compression level '%u' not valid for method '%s': must be [%u,%u] (default=%u)",
            l, method, min, max, methodDefault);
}

static void decompressNone(Source & source, Sink & sink)
{
    std::vector<unsigned char> buf(bufSize);
    while (true) {
        size_t n;
        try {
            n = source.read(buf.data(), buf.size());
        } catch (EndOfFile &) {
            break;
        }
        sink(buf.data(), n);
    }
}

static void decompressXZ(Source & source, Sink & sink)
{
    lzma_stream strm(LZMA_STREAM_INIT);

    lzma_ret ret = lzma_stream_decoder(
        &strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK)
        throw CompressionError("unable to initialise lzma decoder");

    Finally free([&]() { lzma_end(&strm); });

    lzma_action action = LZMA_RUN;
    std::vector<uint8_t> inbuf(bufSize), outbuf(bufSize);
    strm.next_in = nullptr;
    strm.avail_in = 0;
    strm.next_out = outbuf.data();
    strm.avail_out = outbuf.size();
    bool eof = false;

    while (true) {
        checkInterrupt();

        if (strm.avail_in == 0 && !eof) {
            strm.next_in = inbuf.data();
            try {
                strm.avail_in = source.read((unsigned char *) strm.next_in, inbuf.size());
            } catch (EndOfFile &) {
                eof = true;
            }
        }

        if (strm.avail_in == 0)
            action = LZMA_FINISH;

        lzma_ret ret = lzma_code(&strm, action);

        if (strm.avail_out < outbuf.size()) {
            sink((unsigned char *) outbuf.data(), outbuf.size() - strm.avail_out);
            strm.next_out = outbuf.data();
            strm.avail_out = outbuf.size();
        }

        if (ret == LZMA_STREAM_END) return;

        if (ret != LZMA_OK)
            throw CompressionError("error %d while decompressing xz file", ret);
    }
}

static void decompressBzip2(Source & source, Sink & sink)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK)
        throw CompressionError("unable to initialise bzip2 decoder");

    Finally free([&]() { BZ2_bzDecompressEnd(&strm); });

    std::vector<char> inbuf(bufSize), outbuf(bufSize);
    strm.next_in = nullptr;
    strm.avail_in = 0;
    strm.next_out = outbuf.data();
    strm.avail_out = outbuf.size();
    bool eof = false;

    while (true) {
        checkInterrupt();

        if (strm.avail_in == 0 && !eof) {
            strm.next_in = inbuf.data();
            try {
                strm.avail_in = source.read((unsigned char *) strm.next_in, inbuf.size());
            } catch (EndOfFile &) {
                eof = true;
            }
        }

        int ret = BZ2_bzDecompress(&strm);

        if (strm.avail_in == 0 && strm.avail_out == outbuf.size() && eof)
            throw CompressionError("bzip2 data ends prematurely");

        if (strm.avail_out < outbuf.size()) {
            sink((unsigned char *) outbuf.data(), outbuf.size() - strm.avail_out);
            strm.next_out = outbuf.data();
            strm.avail_out = outbuf.size();
        }

        if (ret == BZ_STREAM_END) return;

        if (ret != BZ_OK)
            throw CompressionError("error while decompressing bzip2 file");
    }
}

static void decompressBrotli(Source & source, Sink & sink)
{
#if !HAVE_BROTLI
    RunOptions options(BROTLI, {"-d"});
    options.standardIn = &source;
    options.standardOut = &sink;
    runProgram2(options);
#else
    auto *s = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!s)
        throw CompressionError("unable to initialize brotli decoder");

    Finally free([s]() { BrotliDecoderDestroyInstance(s); });

    std::vector<uint8_t> inbuf(bufSize), outbuf(bufSize);
    const uint8_t * next_in = nullptr;
    size_t avail_in = 0;
    bool eof = false;

    while (true) {
        checkInterrupt();

        if (avail_in == 0 && !eof) {
            next_in = inbuf.data();
            try {
                avail_in = source.read((unsigned char *) next_in, inbuf.size());
            } catch (EndOfFile &) {
                eof = true;
            }
        }

        uint8_t * next_out = outbuf.data();
        size_t avail_out = outbuf.size();

        auto ret = BrotliDecoderDecompressStream(s,
                &avail_in, &next_in,
                &avail_out, &next_out,
                nullptr);

        switch (ret) {
        case BROTLI_DECODER_RESULT_ERROR:
            throw CompressionError("error while decompressing brotli file");
        case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
            if (eof)
                throw CompressionError("incomplete or corrupt brotli file");
            break;
        case BROTLI_DECODER_RESULT_SUCCESS:
            if (avail_in != 0)
                throw CompressionError("unexpected input after brotli decompression");
            break;
        case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
            // I'm not sure if this can happen, but abort if this happens with empty buffer
            if (avail_out == outbuf.size())
                throw CompressionError("brotli decompression requires larger buffer");
            break;
        }

        // Always ensure we have full buffer for next invocation
        if (avail_out < outbuf.size())
            sink((unsigned char *) outbuf.data(), outbuf.size() - avail_out);

        if (ret == BROTLI_DECODER_RESULT_SUCCESS) return;
    }
#endif // HAVE_BROTLI
}

#if HAVE_ZSTD
static void decompressZstd(Source & source, Sink & sink)
{
  auto *s = ZSTD_createDStream();
  if (!s)
    throw CompressionError("unable to initialize zstd decoder");

  Finally free([s]() { ZSTD_freeDStream(s); });

  size_t toRead = ZSTD_initDStream(s);
  if (ZSTD_isError(toRead))
    throw CompressionError("unable to initialize zstd streaming decoder");

  std::vector<uint8_t> inbuf(ZSTD_DStreamInSize());
  std::vector<uint8_t> outbuf(ZSTD_DStreamOutSize());

  while (true) {
    checkInterrupt();

    size_t read = 0;
    try {
      read = source.read(inbuf.data(), toRead);
    } catch (EndOfFile &) {
      // *** This is different than others! ***
      // We're done if no more input
      return;
    }

    ZSTD_inBuffer input{inbuf.data(), read, 0};

    while (input.pos < input.size) {
      checkInterrupt();
      ZSTD_outBuffer output{outbuf.data(), outbuf.size(), 0};
      toRead = ZSTD_decompressStream(s, &output, &input);
      if (ZSTD_isError(toRead))
        throw CompressionError("error while decompressing zstd data");
      sink(outbuf.data(), output.pos);
    }
  }
}
#endif // HAVE_ZSTD

ref<std::string> decompress(const std::string & method, const std::string & in)
{
    StringSource source(in);
    StringSink sink;
    decompress(method, source, sink);
    return sink.s;
}

void decompress(const std::string & method, Source & source, Sink & sink)
{
    if (method == "none")
        return decompressNone(source, sink);
    else if (method == "xz")
        return decompressXZ(source, sink);
    else if (method == "bzip2")
        return decompressBzip2(source, sink);
    else if (method == "br")
        return decompressBrotli(source, sink);
#if HAVE_ZSTD
    else if (method == "zstd")
        return decompressZstd(source, sink);
#endif
    else
        throw UnknownCompressionMethod("unknown compression method '%s'", method);
}

struct NoneSink : CompressionSink
{
    Sink & nextSink;
    NoneSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink) {
        if (level != COMPRESSION_LEVEL_DEFAULT)
            printError("Warning: requested compression level '%d' not supported by compression method 'none'", level);
    }
    void finish() override { flush(); }
    void write(const unsigned char * data, size_t len) override { nextSink(data, len); }
};

struct XzSink : CompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    lzma_stream strm = LZMA_STREAM_INIT;
    bool finished = false;

    template <typename F>
    XzSink(Sink & nextSink, F&& initEncoder, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink) {
        lzma_ret ret = initEncoder(checkLevel(0, 9, LZMA_PRESET_DEFAULT, "xz", level));
        if (ret != LZMA_OK)
            throw CompressionError("unable to initialise lzma encoder");
        // FIXME: apply the x86 BCJ filter?

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }
    XzSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : XzSink(nextSink, [this](unsigned level) {
        return lzma_easy_encoder(&strm, level, LZMA_CHECK_CRC64);
    }, level) {}

    ~XzSink()
    {
        lzma_end(&strm);
    }

    void finish() override
    {
        CompressionSink::flush();

        assert(!finished);
        finished = true;

        while (true) {
            checkInterrupt();

            lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
            if (ret != LZMA_OK && ret != LZMA_STREAM_END)
                throw CompressionError("error while flushing xz file");

            if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
                nextSink(outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }

            if (ret == LZMA_STREAM_END) break;
        }
    }

    void write(const unsigned char * data, size_t len) override
    {
        assert(!finished);

        strm.next_in = data;
        strm.avail_in = len;

        while (strm.avail_in) {
            checkInterrupt();

            lzma_ret ret = lzma_code(&strm, LZMA_RUN);
            if (ret != LZMA_OK)
                throw CompressionError("error while compressing xz file");

            if (strm.avail_out == 0) {
                nextSink(outbuf, sizeof(outbuf));
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

#ifdef HAVE_LZMA_MT
struct ParallelXzSink : public XzSink
{
  ParallelXzSink(Sink &nextSink, int level) : XzSink(nextSink, [this](unsigned level) {
        lzma_mt mt_options = {};
        mt_options.flags = 0;
        mt_options.timeout = 300; // Using the same setting as the xz cmd line
        mt_options.preset = level;
        mt_options.filters = NULL;
        mt_options.check = LZMA_CHECK_CRC64;
        mt_options.threads = lzma_cputhreads();
        mt_options.block_size = 0;
        if (mt_options.threads == 0)
            mt_options.threads = 1;
        // FIXME: maybe use lzma_stream_encoder_mt_memusage() to control the
        // number of threads.
        return lzma_stream_encoder_mt(&strm, &mt_options);
  }, level) {}
};
#endif

struct BzipSink : CompressionSink
{
    Sink & nextSink;
    char outbuf[BUFSIZ];
    bz_stream strm;
    bool finished = false;

    BzipSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        memset(&strm, 0, sizeof(strm));
        auto l = checkLevel(1, 9, 9, "bzip2", level);
        int ret = BZ2_bzCompressInit(&strm, l, 0, 30);
        if (ret != BZ_OK)
            throw CompressionError("unable to initialise bzip2 encoder");

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~BzipSink()
    {
        BZ2_bzCompressEnd(&strm);
    }

    void finish() override
    {
        flush();

        assert(!finished);
        finished = true;

        while (true) {
            checkInterrupt();

            int ret = BZ2_bzCompress(&strm, BZ_FINISH);
            if (ret != BZ_FINISH_OK && ret != BZ_STREAM_END)
                throw CompressionError("error while flushing bzip2 file");

            if (strm.avail_out == 0 || ret == BZ_STREAM_END) {
                nextSink((unsigned char *) outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }

            if (ret == BZ_STREAM_END) break;
        }
    }

    void write(const unsigned char * data, size_t len) override
    {
        /* Bzip2's 'avail_in' parameter is an unsigned int, so we need
           to split the input into chunks of at most 4 GiB. */
        while (len) {
            auto n = std::min((size_t) std::numeric_limits<decltype(strm.avail_in)>::max(), len);
            writeInternal(data, n);
            data += n;
            len -= n;
        }
    }

    void writeInternal(const unsigned char * data, size_t len)
    {
        assert(!finished);
        assert(len <= std::numeric_limits<decltype(strm.avail_in)>::max());

        strm.next_in = (char *) data;
        strm.avail_in = len;

        while (strm.avail_in) {
            checkInterrupt();

            int ret = BZ2_bzCompress(&strm, BZ_RUN);
            if (ret != BZ_OK)
                CompressionError("error while compressing bzip2 file");

            if (strm.avail_out == 0) {
                nextSink((unsigned char *) outbuf, sizeof(outbuf));
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

struct LambdaCompressionSink : CompressionSink
{
    Sink & nextSink;
    std::string data;
    using CompressFnTy = std::function<std::string(const std::string&)>;
    CompressFnTy compressFn;
    LambdaCompressionSink(Sink& nextSink, CompressFnTy compressFn)
        : nextSink(nextSink)
        , compressFn(std::move(compressFn))
    {
    };

    void finish() override
    {
        flush();
        nextSink(compressFn(data));
    }

    void write(const unsigned char * data, size_t len) override
    {
        checkInterrupt();
        this->data.append((const char *) data, len);
    }
};

struct BrotliCmdSink : LambdaCompressionSink
{
    BrotliCmdSink(Sink& nextSink, int level = COMPRESSION_LEVEL_DEFAULT)
        : LambdaCompressionSink(nextSink, [level](const std::string& data) {
            // Hard-code values from brotli manpage
            std::string levelArg = fmt("-%u", checkLevel(0, 11, 11, "brotli", level));
            return runProgram(BROTLI, true, {levelArg}, data);
        })
    {
    }
};

#if HAVE_BROTLI
struct BrotliSink : CompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    BrotliEncoderState *state;
    bool finished = false;

    BrotliSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state)
            throw CompressionError("unable to initialise brotli encoder");

        if (!BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY,
                checkLevel(BROTLI_MIN_QUALITY, BROTLI_MAX_QUALITY,
                    BROTLI_DEFAULT_QUALITY, "brotli", level)))
            throw CompressionError("failure setting requested compression level for brotli encoder");
    }

    ~BrotliSink()
    {
        BrotliEncoderDestroyInstance(state);
    }

    void finish() override
    {
        flush();
        assert(!finished);

        const uint8_t *next_in = nullptr;
        size_t avail_in = 0;
        uint8_t *next_out = outbuf;
        size_t avail_out = sizeof(outbuf);
        while (!finished) {
            checkInterrupt();

            if (!BrotliEncoderCompressStream(state,
                        BROTLI_OPERATION_FINISH,
                        &avail_in, &next_in,
                        &avail_out, &next_out,
                        nullptr))
                throw CompressionError("error while finishing brotli file");

            finished = BrotliEncoderIsFinished(state);
            if (avail_out == 0 || finished) {
                nextSink(outbuf, sizeof(outbuf) - avail_out);
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }
        }
    }

    void write(const unsigned char * data, size_t len) override
    {
        // Don't feed brotli too much at once
        const size_t CHUNK_SIZE = sizeof(outbuf) << 2;
        while (len) {
          size_t n = std::min(CHUNK_SIZE, len);
          writeInternal(data, n);
          data += n;
          len -= n;
        }
    }

    void writeInternal(const unsigned char * data, size_t len)
    {
        assert(!finished);

        const uint8_t *next_in = data;
        size_t avail_in = len;
        uint8_t *next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (avail_in > 0) {
            checkInterrupt();

            if (!BrotliEncoderCompressStream(state,
                      BROTLI_OPERATION_PROCESS,
                      &avail_in, &next_in,
                      &avail_out, &next_out,
                      nullptr))
                throw CompressionError("error while compressing brotli file");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - avail_out);
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }
        }
    }
};
#endif // HAVE_BROTLI

#if HAVE_ZSTD
struct ZstdSink: CompressionSink
{
    Sink & nextSink;
    std::vector<uint8_t> outbuf;
    ZSTD_CStream *state;

    bool finished = false;

    ZstdSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT)
        : nextSink(nextSink), outbuf(std::max<size_t>(ZSTD_CStreamOutSize(), BUFSIZ))
    {
        state = ZSTD_createCStream();
        if (!state)
            throw CompressionError("unable to initialise zstd encoder");

        auto r = ZSTD_initCStream(state, checkLevel(1, 19, ZSTD_CLEVEL_DEFAULT, "zstd", level));
        if (ZSTD_isError(r))
          throw CompressionError("unable to initialise zstd encoder");
    }

    ~ZstdSink()
    {
        ZSTD_freeCStream(state);
    }

    void finish() override
    {
        flush();
        assert(!finished);

        ZSTD_outBuffer output{outbuf.data(), outbuf.size(), 0};

        auto r = ZSTD_endStream(state, &output);
        if (r > 0)
           throw CompressionError("zstd not flushed, bytes remaining: %zd", r);
        else if (ZSTD_isError(r))
          throw CompressionError("error finish'ing zstd stream");

        nextSink(outbuf.data(), output.pos);
    }

    void write(const unsigned char * data, size_t len) override
    {
        ZSTD_inBuffer input{data, len, 0};
        while (input.pos < input.size) {
          ZSTD_outBuffer output{outbuf.data(), outbuf.size(), 0};
          auto r = ZSTD_compressStream(state, &output, &input);
          if (ZSTD_isError(r))
            throw CompressionError("error compressing with zstd");
          // (zstd suggests amount that should be 'read' next,
          // which we ignore since we can't make use of it currently)

          nextSink(outbuf.data(), output.pos);
        }
    }
};
#endif // HAVE_ZSTD

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel, int level)
{
    if (parallel) {
#ifdef HAVE_LZMA_MT
        if (method == "xz")
            return make_ref<ParallelXzSink>(nextSink, level);
#endif
        printMsg(lvlError, format("Warning: parallel compression requested but not supported for method '%1%', falling back to single-threaded compression") % method);
    }

    if (method == "none")
        return make_ref<NoneSink>(nextSink, level);
    else if (method == "xz")
        return make_ref<XzSink>(nextSink, level);
    else if (method == "bzip2")
        return make_ref<BzipSink>(nextSink, level);
    else if (method == "br")
#if HAVE_BROTLI
        return make_ref<BrotliSink>(nextSink, level);
#else
        return make_ref<BrotliCmdSink>(nextSink, level);
#endif
#if HAVE_ZSTD
    else if (method == "zstd")
        return make_ref<ZstdSink>(nextSink, level);
#endif
    else
        throw UnknownCompressionMethod(format("unknown compression method '%s'") % method);
}

ref<std::string> compress(const std::string & method, const std::string & in, const bool parallel, int level)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink, parallel, level);
    (*sink)(in);
    sink->finish();
    return ssink.s;
}

}
