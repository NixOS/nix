#include "compression.hh"
#include "util.hh"
#include "finally.hh"

#include <lzma.h>
#include <bzlib.h>
#include <cstdio>
#include <cstring>

namespace nix {

static ref<std::string> compressXZ(const std::string & in)
{
    lzma_stream strm(LZMA_STREAM_INIT);

    // FIXME: apply the x86 BCJ filter?

    lzma_ret ret = lzma_easy_encoder(
        &strm, 6, LZMA_CHECK_CRC64);
    if (ret != LZMA_OK)
        throw Error("unable to initialise lzma encoder");

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
            throw Error("error while compressing xz file");
    }
}

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

static ref<std::string> compressBzip2(const std::string & in)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = BZ2_bzCompressInit(&strm, 9, 0, 30);
    if (ret != BZ_OK)
        throw Error("unable to initialise bzip2 encoder");

    Finally free([&]() { BZ2_bzCompressEnd(&strm); });

    int action = BZ_RUN;
    char outbuf[BUFSIZ];
    ref<std::string> res = make_ref<std::string>();
    strm.next_in = (char *) in.c_str();
    strm.avail_in = in.size();
    strm.next_out = outbuf;
    strm.avail_out = sizeof(outbuf);

    while (true) {
        checkInterrupt();

        if (strm.avail_in == 0)
            action = BZ_FINISH;

        int ret = BZ2_bzCompress(&strm, action);

        if (strm.avail_out == 0 || ret == BZ_STREAM_END) {
            res->append(outbuf, sizeof(outbuf) - strm.avail_out);
            strm.next_out = outbuf;
            strm.avail_out = sizeof(outbuf);
        }

        if (ret == BZ_STREAM_END)
            return res;

        if (ret != BZ_OK && ret != BZ_FINISH_OK)
             Error("error while compressing bzip2 file");
    }

    return res;
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

ref<std::string> compress(const std::string & method, ref<std::string> in)
{
    if (method == "none")
        return in;
    else if (method == "xz")
        return compressXZ(*in);
    else if (method == "bzip2")
        return compressBzip2(*in);
    else
        throw UnknownCompressionMethod(format("unknown compression method ‘%s’") % method);
}

ref<std::string> decompress(const std::string & method, ref<std::string> in)
{
    if (method == "none")
        return in;
    else if (method == "xz")
        return decompressXZ(*in);
    else if (method == "bzip2")
        return decompressBzip2(*in);
    else
        throw UnknownCompressionMethod(format("unknown compression method ‘%s’") % method);
}

}
