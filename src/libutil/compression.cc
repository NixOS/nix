#include "compression.hh"
#include "tarfile.hh"
#include "util.hh"
#include "finally.hh"
#include "logging.hh"

#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <cstring>

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <iostream>

namespace nix {

static const int COMPRESSION_LEVEL_DEFAULT = -1;

// Don't feed brotli too much at once.
struct ChunkedCompressionSink : CompressionSink
{
    uint8_t outbuf[32 * 1024];

    void write(std::string_view data) override
    {
        const size_t CHUNK_SIZE = sizeof(outbuf) << 2;
        while (!data.empty()) {
            size_t n = std::min(CHUNK_SIZE, data.size());
            writeInternal(data.substr(0, n));
            data.remove_prefix(n);
        }
    }

    virtual void writeInternal(std::string_view data) = 0;
};

struct ArchiveDecompressionSource : Source
{
    std::unique_ptr<TarArchive> archive = 0;
    Source & src;
    ArchiveDecompressionSource(Source & src) : src(src) {}
    ~ArchiveDecompressionSource() override {}
    size_t read(char * data, size_t len) override {
        struct archive_entry * ae;
        if (!archive) {
            archive = std::make_unique<TarArchive>(src, true);
            this->archive->check(archive_read_next_header(this->archive->archive, &ae),
                "failed to read header (%s)");
            if (archive_filter_count(this->archive->archive) < 2) {
                throw CompressionError("input compression not recognized");
            }
        }
        ssize_t result = archive_read_data(this->archive->archive, data, len);
        if (result > 0) return result;
        if (result == 0) {
            throw EndOfFile("reached end of compressed file");
        }
        this->archive->check(result, "failed to read compressed data (%s)");
        return result;
    }
};

struct ArchiveCompressionSink : CompressionSink
{
    Sink & nextSink;
    struct archive * archive;

    ArchiveCompressionSink(Sink & nextSink, std::string format, bool parallel, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        archive = archive_write_new();
        if (!archive) throw Error("failed to initialize libarchive");
        check(archive_write_add_filter_by_name(archive, format.c_str()), "couldn't initialize compression (%s)");
        check(archive_write_set_format_raw(archive));
        if (parallel)
            check(archive_write_set_filter_option(archive, format.c_str(), "threads", "0"));
        if (level != COMPRESSION_LEVEL_DEFAULT)
            check(archive_write_set_filter_option(archive, format.c_str(), "compression-level", std::to_string(level).c_str()));
        // disable internal buffering
        check(archive_write_set_bytes_per_block(archive, 0));
        // disable output padding
        check(archive_write_set_bytes_in_last_block(archive, 1));
        open();
    }

    ~ArchiveCompressionSink() override
    {
        if (archive) archive_write_free(archive);
    }

    void finish() override
    {
        flush();
        check(archive_write_close(archive));
    }

    void check(int err, const std::string & reason = "failed to compress (%s)")
    {
        if (err == ARCHIVE_EOF)
            throw EndOfFile("reached end of archive");
        else if (err != ARCHIVE_OK)
            throw Error(reason, archive_error_string(this->archive));
    }

    void write(std::string_view data) override
    {
        ssize_t result = archive_write_data(archive, data.data(), data.length());
        if (result <= 0) check(result);
    }

private:
    void open()
    {
        check(archive_write_open(archive, this, nullptr, ArchiveCompressionSink::callback_write, nullptr));
        auto ae = archive_entry_new();
        archive_entry_set_filetype(ae, AE_IFREG);
        check(archive_write_header(archive, ae));
        archive_entry_free(ae);
    }

    static ssize_t callback_write(struct archive * archive, void * _self, const void * buffer, size_t length)
    {
        auto self = (ArchiveCompressionSink *) _self;
        self->nextSink({(const char *) buffer, length});
        return length;
    }
};

struct NoneSink : CompressionSink
{
    Sink & nextSink;
    NoneSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        if (level != COMPRESSION_LEVEL_DEFAULT)
            warn("requested compression level '%d' not supported by compression method 'none'", level);
    }
    void finish() override { flush(); }
    void write(std::string_view data) override { nextSink(data); }
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
        writeInternal({});
    }

    void writeInternal(std::string_view data) override
    {
        auto next_in = (const uint8_t *) data.data();
        size_t avail_in = data.size();
        uint8_t * next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (!finished && (!data.data() || avail_in)) {
            checkInterrupt();

            if (!BrotliDecoderDecompressStream(state,
                    &avail_in, &next_in,
                    &avail_out, &next_out,
                    nullptr))
                throw CompressionError("error while decompressing brotli file");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink({(char *) outbuf, sizeof(outbuf) - avail_out});
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }

            finished = BrotliDecoderIsFinished(state);
        }
    }
};

std::string decompress(const std::string & method, std::string_view in)
{
    StringSink ssink;
    auto sink = makeDecompressionSink(method, ssink);
    (*sink)(in);
    sink->finish();
    return std::move(ssink.s);
}

std::unique_ptr<FinishSink> makeDecompressionSink(const std::string & method, Sink & nextSink)
{
    if (method == "none" || method == "")
        return std::make_unique<NoneSink>(nextSink);
    else if (method == "br")
        return std::make_unique<BrotliDecompressionSink>(nextSink);
    else
        return sourceToSink([&](Source & source) {
            auto decompressionSource = std::make_unique<ArchiveDecompressionSource>(source);
            decompressionSource->drainInto(nextSink);
        });
}

struct BrotliCompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    BrotliEncoderState * state;
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
        writeInternal({});
    }

    void writeInternal(std::string_view data) override
    {
        auto next_in = (const uint8_t *) data.data();
        size_t avail_in = data.size();
        uint8_t * next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (!finished && (!data.data() || avail_in)) {
            checkInterrupt();

            if (!BrotliEncoderCompressStream(state,
                    data.data() ? BROTLI_OPERATION_PROCESS : BROTLI_OPERATION_FINISH,
                    &avail_in, &next_in,
                    &avail_out, &next_out,
                    nullptr))
                throw CompressionError("error while compressing brotli compression");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink({(const char *) outbuf, sizeof(outbuf) - avail_out});
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }

            finished = BrotliEncoderIsFinished(state);
        }
    }
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel, int level)
{
    std::vector<std::string> la_supports = {
        "bzip2", "compress", "grzip", "gzip", "lrzip", "lz4", "lzip", "lzma", "lzop", "xz", "zstd"
    };
    if (std::find(la_supports.begin(), la_supports.end(), method) != la_supports.end()) {
        return make_ref<ArchiveCompressionSink>(nextSink, method, parallel, level);
    }
    if (method == "none")
        return make_ref<NoneSink>(nextSink);
    else if (method == "br")
        return make_ref<BrotliCompressionSink>(nextSink);
    else
        throw UnknownCompressionMethod("unknown compression method '%s'", method);
}

std::string compress(const std::string & method, std::string_view in, const bool parallel, int level)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink, parallel, level);
    (*sink)(in);
    sink->finish();
    return std::move(ssink.s);
}

}
