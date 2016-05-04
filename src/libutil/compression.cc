#include "compression.hh"
#include "util.hh"
#include "finally.hh"

#include <lzma.h>
#include <bzlib.h>
#include <cstdio>
#include <cstring>

#include <iostream>

namespace nix {

static ref<std::string> decompressXZ(const std::string & in)
{
    lzma_stream strm(LZMA_STREAM_INIT);

    lzma_ret ret = lzma_stream_decoder(
        &strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK)
        throw Error("unable to initialise lzma decoder");

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
            throw Error("error while decompressing xz file");
    }
}

static ref<std::string> decompressBzip2(const std::string & in)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK)
        throw Error("unable to initialise bzip2 decoder");

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
            throw Error("error while decompressing bzip2 file");
    }
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
    else
        throw UnknownCompressionMethod(format("unknown compression method ‘%s’") % method);
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
        lzma_ret ret = lzma_easy_encoder(
            &strm, 6, LZMA_CHECK_CRC64);
        if (ret != LZMA_OK)
            throw Error("unable to initialise lzma encoder");
        // FIXME: apply the x86 BCJ filter?

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~XzSink()
    {
        assert(finished);
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
                throw Error("error while flushing xz file");

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
                throw Error("error while compressing xz file");

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
            throw Error("unable to initialise bzip2 encoder");

        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
    }

    ~BzipSink()
    {
        assert(finished);
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
                throw Error("error while flushing bzip2 file");

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
                Error("error while compressing bzip2 file");

            if (strm.avail_out == 0) {
                nextSink((unsigned char *) outbuf, sizeof(outbuf));
                strm.next_out = outbuf;
                strm.avail_out = sizeof(outbuf);
            }
        }
    }
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink)
{
    if (method == "none")
        return make_ref<NoneSink>(nextSink);
    else if (method == "xz")
        return make_ref<XzSink>(nextSink);
    else if (method == "bzip2")
        return make_ref<BzipSink>(nextSink);
    else
        throw UnknownCompressionMethod(format("unknown compression method ‘%s’") % method);
}

}
