#include "compression.hh"
#include "util.hh"
#include "finally.hh"

#include <lzma.h>
#include <bzlib.h>
#include <cstdio>
#include <cstring>

#if HAVE_BROTLI
#include <brotli/decode.h>
#include <brotli/encode.h>
#endif // HAVE_BROTLI

#include <iostream>

namespace nix {

static ref<std::string> decompressXZ(const std::string & in)
{
    lzma_stream strm(LZMA_STREAM_INIT);

    lzma_ret ret = lzma_stream_decoder(
        &strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK)
        throw CompressionError("unable to initialise lzma decoder");

    Finally free([&]() { lzma_end(&strm); });

    lzma_action action = LZMA_RUN;
    uint8_t outbuf[BUFSIZ];
    ref<std::string> res = make_ref<std::string>();
    strm.next_in = (uint8_t *) in.c_str();
    strm.avail_in = in.size();
    strm.next_out = outbuf;
    strm.avail_out = sizeof(outbuf);

    while (true) {
        checkInterrupt();

        if (strm.avail_in == 0)
            action = LZMA_FINISH;

        lzma_ret ret = lzma_code(&strm, action);

        if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
            res->append((char *) outbuf, sizeof(outbuf) - strm.avail_out);
            strm.next_out = outbuf;
            strm.avail_out = sizeof(outbuf);
        }

        if (ret == LZMA_STREAM_END)
            return res;

        if (ret != LZMA_OK)
            throw CompressionError("error %d while decompressing xz file", ret);
    }
}

static ref<std::string> decompressBzip2(const std::string & in)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK)
        throw CompressionError("unable to initialise bzip2 decoder");

    Finally free([&]() { BZ2_bzDecompressEnd(&strm); });

    char outbuf[BUFSIZ];
    ref<std::string> res = make_ref<std::string>();
    strm.next_in = (char *) in.c_str();
    strm.avail_in = in.size();
    strm.next_out = outbuf;
    strm.avail_out = sizeof(outbuf);

    while (true) {
        checkInterrupt();

        int ret = BZ2_bzDecompress(&strm);

        if (strm.avail_out == 0 || ret == BZ_STREAM_END) {
            res->append(outbuf, sizeof(outbuf) - strm.avail_out);
            strm.next_out = outbuf;
            strm.avail_out = sizeof(outbuf);
        }

        if (ret == BZ_STREAM_END)
            return res;

        if (ret != BZ_OK)
            throw CompressionError("error while decompressing bzip2 file");

        if (strm.avail_in == 0)
            throw CompressionError("bzip2 data ends prematurely");
    }
}

static ref<std::string> decompressBrotli(const std::string & in)
{
#if !HAVE_BROTLI
    return make_ref<std::string>(runProgram(BROTLI, true, {"-d"}, {in}));
#else
    auto *s = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!s)
        throw CompressionError("unable to initialize brotli decoder");

    Finally free([s]() { BrotliDecoderDestroyInstance(s); });

    uint8_t outbuf[BUFSIZ];
    ref<std::string> res = make_ref<std::string>();
    const uint8_t *next_in = (uint8_t *)in.c_str();
    size_t avail_in = in.size();
    uint8_t *next_out = outbuf;
    size_t avail_out = sizeof(outbuf);

    while (true) {
        checkInterrupt();

        auto ret = BrotliDecoderDecompressStream(s,
                &avail_in, &next_in,
                &avail_out, &next_out,
                nullptr);

        switch (ret) {
        case BROTLI_DECODER_RESULT_ERROR:
            throw CompressionError("error while decompressing brotli file");
        case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
            throw CompressionError("incomplete or corrupt brotli file");
        case BROTLI_DECODER_RESULT_SUCCESS:
            if (avail_in != 0)
                throw CompressionError("unexpected input after brotli decompression");
            break;
        case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
            // I'm not sure if this can happen, but abort if this happens with empty buffer
            if (avail_out == sizeof(outbuf))
                throw CompressionError("brotli decompression requires larger buffer");
            break;
        }

        // Always ensure we have full buffer for next invocation
        if (avail_out < sizeof(outbuf)) {
            res->append((char*)outbuf, sizeof(outbuf) - avail_out);
            next_out = outbuf;
            avail_out = sizeof(outbuf);
        }

        if (ret == BROTLI_DECODER_RESULT_SUCCESS) return res;
    }
#endif // HAVE_BROTLI
}

ref<std::string> compress(const std::string & method, const std::string & in)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink);
    (*sink)(in);
    sink->finish();
    return ssink.s;
}

ref<std::string> decompress(const std::string & method, const std::string & in)
{
    if (method == "none")
        return make_ref<std::string>(in);
    else if (method == "xz")
        return decompressXZ(in);
    else if (method == "bzip2")
        return decompressBzip2(in);
    else if (method == "br")
        return decompressBrotli(in);
    else
        throw UnknownCompressionMethod(format("unknown compression method '%s'") % method);
}

struct NoneSink : CompressionSink
{
    Sink & nextSink;
    NoneSink(Sink & nextSink) : nextSink(nextSink) { }
    void finish() override { flush(); }
    void write(const unsigned char * data, size_t len) override { nextSink(data, len); }
};

struct XzSink : CompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    lzma_stream strm = LZMA_STREAM_INIT;
    bool finished = false;

    XzSink(Sink & nextSink) : nextSink(nextSink)
    {
        lzma_mt mt_options = {};
        mt_options.flags = 0;
        mt_options.timeout = 300;
        mt_options.check = LZMA_CHECK_CRC64;
        mt_options.threads = lzma_cputhreads();
        lzma_ret ret = lzma_stream_encoder_mt(
            &strm, &mt_options);
        if (ret != LZMA_OK)
            throw CompressionError("unable to initialise lzma encoder");
        // FIXME: apply the x86 BCJ filter?

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

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

struct BzipSink : CompressionSink
{
    Sink & nextSink;
    char outbuf[BUFSIZ];
    bz_stream strm;
    bool finished = false;

    BzipSink(Sink & nextSink) : nextSink(nextSink)
    {
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzCompressInit(&strm, 9, 0, 30);
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
        assert(!finished);

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
    BrotliCmdSink(Sink& nextSink)
        : LambdaCompressionSink(nextSink, [](const std::string& data) {
            return runProgram(BROTLI, true, {}, data);
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

    BrotliSink(Sink & nextSink) : nextSink(nextSink)
    {
        state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state)
            throw CompressionError("unable to initialise brotli encoder");
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
        assert(!finished);

        // Don't feed brotli too much at once
        const size_t CHUNK_SIZE = sizeof(outbuf) << 2;
        while (len) {
          size_t n = std::min(CHUNK_SIZE, len);
          writeInternal(data, n);
          data += n;
          len -= n;
        }
    }
  private:
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

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink)
{
    if (method == "none")
        return make_ref<NoneSink>(nextSink);
    else if (method == "xz")
        return make_ref<XzSink>(nextSink);
    else if (method == "bzip2")
        return make_ref<BzipSink>(nextSink);
    else if (method == "br")
#if HAVE_BROTLI
        return make_ref<BrotliSink>(nextSink);
#else
        return make_ref<BrotliCmdSink>(nextSink);
#endif
    else
        throw UnknownCompressionMethod(format("unknown compression method '%s'") % method);
}

}
