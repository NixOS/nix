#include "compression.hh"
#include "util.hh"
#include "finally.hh"
#include "logging.hh"

#include <lzma.h>
#include <bzlib.h>
#include <cstdio>
#include <cstring>

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <iostream>

namespace nix {

// Don't feed brotli too much at once.
struct ChunkedCompressionSink : CompressionSink
{
    uint8_t outbuf[32 * 1024];

    void write(const unsigned char * data, size_t len) override
    {
        const size_t CHUNK_SIZE = sizeof(outbuf) << 2;
        while (len) {
            size_t n = std::min(CHUNK_SIZE, len);
            writeInternal(data, n);
            data += n;
            len -= n;
        }
    }

    virtual void writeInternal(const unsigned char * data, size_t len) = 0;
};

struct NoneSink : CompressionSink
{
    Sink & nextSink;
    NoneSink(Sink & nextSink) : nextSink(nextSink) { }
    void finish() override { flush(); }
    void write(const unsigned char * data, size_t len) override { nextSink(data, len); }
};

struct XzDecompressionSink : CompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    lzma_stream strm = LZMA_STREAM_INIT;
    bool finished = false;

    XzDecompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        lzma_ret ret = lzma_stream_decoder(
            &strm, UINT64_MAX, LZMA_CONCATENATED);
        if (ret != LZMA_OK)
            throw CompressionError("unable to initialise lzma decoder");

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~XzDecompressionSink()
    {
        lzma_end(&strm);
    }

    void finish() override
    {
        CompressionSink::flush();
        write(nullptr, 0);
    }

    void write(const unsigned char * data, size_t len) override
    {
        strm.next_in = data;
        strm.avail_in = len;

        while (!finished && (!data || strm.avail_in)) {
            checkInterrupt();

            lzma_ret ret = lzma_code(&strm, data ? LZMA_RUN : LZMA_FINISH);
            if (ret != LZMA_OK && ret != LZMA_STREAM_END)
                throw CompressionError("error %d while decompressing xz file", ret);

            finished = ret == LZMA_STREAM_END;

            if (strm.avail_out < sizeof(outbuf) || strm.avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

struct BzipDecompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    bz_stream strm;
    bool finished = false;

    BzipDecompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzDecompressInit(&strm, 0, 0);
        if (ret != BZ_OK)
            throw CompressionError("unable to initialise bzip2 decoder");

        strm.next_out = (char *) outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~BzipDecompressionSink()
    {
        BZ2_bzDecompressEnd(&strm);
    }

    void finish() override
    {
        flush();
        write(nullptr, 0);
    }

    void writeInternal(const unsigned char * data, size_t len) override
    {
        assert(len <= std::numeric_limits<decltype(strm.avail_in)>::max());

        strm.next_in = (char *) data;
        strm.avail_in = len;

        while (strm.avail_in) {
            checkInterrupt();

            int ret = BZ2_bzDecompress(&strm);
            if (ret != BZ_OK && ret != BZ_STREAM_END)
                throw CompressionError("error while decompressing bzip2 file");

            finished = ret == BZ_STREAM_END;

            if (strm.avail_out < sizeof(outbuf) || strm.avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = (char *) outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

struct BrotliDecompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    BrotliDecoderState * state;
    bool finished = false;

    BrotliDecompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state)
            throw CompressionError("unable to initialize brotli decoder");
    }

    ~BrotliDecompressionSink()
    {
        BrotliDecoderDestroyInstance(state);
    }

    void finish() override
    {
        flush();
        writeInternal(nullptr, 0);
    }

    void writeInternal(const unsigned char * data, size_t len) override
    {
        const uint8_t * next_in = data;
        size_t avail_in = len;
        uint8_t * next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (!finished && (!data || avail_in)) {
            checkInterrupt();

            if (!BrotliDecoderDecompressStream(state,
                    &avail_in, &next_in,
                    &avail_out, &next_out,
                    nullptr))
                throw CompressionError("error while decompressing brotli file");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - avail_out);
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }

            finished = BrotliDecoderIsFinished(state);
        }
    }
};

ref<std::string> decompress(const std::string & method, const std::string & in)
{
    StringSink ssink;
    auto sink = makeDecompressionSink(method, ssink);
    (*sink)(in);
    sink->finish();
    return ssink.s;
}

ref<CompressionSink> makeDecompressionSink(const std::string & method, Sink & nextSink)
{
    if (method == "none" || method == "")
        return make_ref<NoneSink>(nextSink);
    else if (method == "xz")
        return make_ref<XzDecompressionSink>(nextSink);
    else if (method == "bzip2")
        return make_ref<BzipDecompressionSink>(nextSink);
    else if (method == "br")
        return make_ref<BrotliDecompressionSink>(nextSink);
    else
        throw UnknownCompressionMethod("unknown compression method '%s'", method);
}

struct XzCompressionSink : CompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    lzma_stream strm = LZMA_STREAM_INIT;
    bool finished = false;

    XzCompressionSink(Sink & nextSink, bool parallel) : nextSink(nextSink)
    {
        lzma_ret ret;
        bool done = false;

        if (parallel) {
#ifdef HAVE_LZMA_MT
            lzma_mt mt_options = {};
            mt_options.flags = 0;
            mt_options.timeout = 300; // Using the same setting as the xz cmd line
            mt_options.preset = LZMA_PRESET_DEFAULT;
            mt_options.filters = NULL;
            mt_options.check = LZMA_CHECK_CRC64;
            mt_options.threads = lzma_cputhreads();
            mt_options.block_size = 0;
            if (mt_options.threads == 0)
                mt_options.threads = 1;
            // FIXME: maybe use lzma_stream_encoder_mt_memusage() to control the
            // number of threads.
            ret = lzma_stream_encoder_mt(&strm, &mt_options);
            done = true;
#else
            printMsg(lvlError, "warning: parallel XZ compression requested but not supported, falling back to single-threaded compression");
#endif
        }

        if (!done)
            ret = lzma_easy_encoder(&strm, 6, LZMA_CHECK_CRC64);

        if (ret != LZMA_OK)
            throw CompressionError("unable to initialise lzma encoder");

        // FIXME: apply the x86 BCJ filter?

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~XzCompressionSink()
    {
        lzma_end(&strm);
    }

    void finish() override
    {
        CompressionSink::flush();
        write(nullptr, 0);
    }

    void write(const unsigned char * data, size_t len) override
    {
        strm.next_in = data;
        strm.avail_in = len;

        while (!finished && (!data || strm.avail_in)) {
            checkInterrupt();

            lzma_ret ret = lzma_code(&strm, data ? LZMA_RUN : LZMA_FINISH);
            if (ret != LZMA_OK && ret != LZMA_STREAM_END)
                throw CompressionError("error %d while compressing xz file", ret);

            finished = ret == LZMA_STREAM_END;

            if (strm.avail_out < sizeof(outbuf) || strm.avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

struct BzipCompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    bz_stream strm;
    bool finished = false;

    BzipCompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzCompressInit(&strm, 9, 0, 30);
        if (ret != BZ_OK)
            throw CompressionError("unable to initialise bzip2 encoder");

        strm.next_out = (char *) outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~BzipCompressionSink()
    {
        BZ2_bzCompressEnd(&strm);
    }

    void finish() override
    {
        flush();
        writeInternal(nullptr, 0);
    }

    void writeInternal(const unsigned char * data, size_t len) override
    {
        assert(len <= std::numeric_limits<decltype(strm.avail_in)>::max());

        strm.next_in = (char *) data;
        strm.avail_in = len;

        while (!finished && (!data || strm.avail_in)) {
            checkInterrupt();

            int ret = BZ2_bzCompress(&strm, data ? BZ_RUN : BZ_FINISH);
            if (ret != BZ_RUN_OK && ret != BZ_FINISH_OK && ret != BZ_STREAM_END)
                throw CompressionError("error %d while compressing bzip2 file", ret);

            finished = ret == BZ_STREAM_END;

            if (strm.avail_out < sizeof(outbuf) || strm.avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - strm.avail_out);
                strm.next_out = (char *) outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

struct BrotliCompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    BrotliEncoderState *state;
    bool finished = false;

    BrotliCompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state)
            throw CompressionError("unable to initialise brotli encoder");
    }

    ~BrotliCompressionSink()
    {
        BrotliEncoderDestroyInstance(state);
    }

    void finish() override
    {
        flush();
        writeInternal(nullptr, 0);
    }

    void writeInternal(const unsigned char * data, size_t len) override
    {
        const uint8_t * next_in = data;
        size_t avail_in = len;
        uint8_t * next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (!finished && (!data || avail_in)) {
            checkInterrupt();

            if (!BrotliEncoderCompressStream(state,
                    data ? BROTLI_OPERATION_PROCESS : BROTLI_OPERATION_FINISH,
                    &avail_in, &next_in,
                    &avail_out, &next_out,
                    nullptr))
                throw CompressionError("error while compressing brotli compression");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink(outbuf, sizeof(outbuf) - avail_out);
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }

            finished = BrotliEncoderIsFinished(state);
        }
    }
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel)
{
    if (method == "none")
        return make_ref<NoneSink>(nextSink);
    else if (method == "xz")
        return make_ref<XzCompressionSink>(nextSink, parallel);
    else if (method == "bzip2")
        return make_ref<BzipCompressionSink>(nextSink);
    else if (method == "br")
        return make_ref<BrotliCompressionSink>(nextSink);
    else
        throw UnknownCompressionMethod(format("unknown compression method '%s'") % method);
}

ref<std::string> compress(const std::string & method, const std::string & in, const bool parallel)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink, parallel);
    (*sink)(in);
    sink->finish();
    return ssink.s;
}

}
