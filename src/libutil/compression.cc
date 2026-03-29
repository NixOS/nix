#include "nix/util/compression.hh"
#include "nix/util/signals.hh"
#include "nix/util/tarfile.hh"
#include "nix/util/finally.hh"
#include "nix/util/logging.hh"

#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <cstring>

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <zstd.h>
#include <thread>

namespace nix {

static const int COMPRESSION_LEVEL_DEFAULT = -1;

// Don't feed brotli too much at once.
struct ChunkedCompressionSink : CompressionSink
{
    uint8_t outbuf[32 * 1024];

    void writeUnbuffered(std::string_view data) override
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
    std::optional<std::string> compressionMethod;

    ArchiveDecompressionSource(Source & src, std::optional<std::string> compressionMethod = std::nullopt)
        : src(src)
        , compressionMethod(std::move(compressionMethod))
    {
    }

    ~ArchiveDecompressionSource() override {}

    size_t read(char * data, size_t len) override
    {
        struct archive_entry * ae;
        if (!archive) {
            archive = std::make_unique<TarArchive>(src, /*raw*/ true, compressionMethod);
            this->archive->check(archive_read_next_header(this->archive->archive, &ae), "failed to read header (%s)");
            if (archive_filter_count(this->archive->archive) < 2) {
                throw CompressionError("input compression not recognized");
            }
        }
        ssize_t result = archive_read_data(this->archive->archive, data, len);
        if (result > 0)
            return result;
        if (result == 0) {
            throw EndOfFile("reached end of compressed file");
        }
        this->archive->check(result, "failed to read compressed data (%s)");
        return result;
    }
};

/* Algorithms handled by libarchive.  zstd is intentionally absent —
   it is handled by ZstdMultiFrameCompressionSink for compression
   (emitting independent frames that enable future parallel
   decompression), while libarchive still handles zstd
   *decompression* via ArchiveDecompressionSource. */
#define NIX_FOR_EACH_LA_ALGO(MACRO) \
    MACRO(bzip2)                    \
    MACRO(compress)                 \
    MACRO(grzip)                    \
    MACRO(gzip)                     \
    MACRO(lrzip)                    \
    MACRO(lz4)                      \
    MACRO(lzip)                     \
    MACRO(lzma)                     \
    MACRO(lzop)                     \
    MACRO(xz)

struct ArchiveCompressionSink : CompressionSink
{
    Sink & nextSink;
    struct archive * archive;

    ArchiveCompressionSink(
        Sink & nextSink, CompressionAlgo method, bool parallel, int level = COMPRESSION_LEVEL_DEFAULT)
        : nextSink(nextSink)
    {
        archive = archive_write_new();
        if (!archive)
            throw Error("failed to initialize libarchive");

        auto [addFilter, format] = [method]() -> std::pair<int (*)(struct archive *), const char *> {
            switch (method) {
            case CompressionAlgo::none:
            case CompressionAlgo::brotli:
            case CompressionAlgo::zstd:
                unreachable();
#define NIX_DEF_LA_ALGO_CASE(algo) \
    case CompressionAlgo::algo:    \
        return {archive_write_add_filter_##algo, #algo};
                NIX_FOR_EACH_LA_ALGO(NIX_DEF_LA_ALGO_CASE)
#undef NIX_DEF_LA_ALGO_CASE
            }
            unreachable();
        }();

        check(addFilter(archive), "couldn't initialize compression (%s)");
        check(archive_write_set_format_raw(archive));
        if (parallel)
            check(archive_write_set_filter_option(archive, format, "threads", "0"));
        if (level != COMPRESSION_LEVEL_DEFAULT)
            check(archive_write_set_filter_option(archive, format, "compression-level", std::to_string(level).c_str()));
        // disable internal buffering
        check(archive_write_set_bytes_per_block(archive, 0));
        // disable output padding
        check(archive_write_set_bytes_in_last_block(archive, 1));
        open();
    }

    ~ArchiveCompressionSink() override
    {
        if (archive)
            archive_write_free(archive);
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

    void writeUnbuffered(std::string_view data) override
    {
        ssize_t result = archive_write_data(archive, data.data(), data.length());
        if (result <= 0)
            check(result);
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

    NoneSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT)
        : nextSink(nextSink)
    {
        if (level != COMPRESSION_LEVEL_DEFAULT)
            warn("requested compression level '%d' not supported by compression method 'none'", level);
    }

    void finish() override
    {
        flush();
    }

    void writeUnbuffered(std::string_view data) override
    {
        nextSink(data);
    }
};

struct BrotliDecompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    BrotliDecoderState * state;
    bool finished = false;

    BrotliDecompressionSink(Sink & nextSink)
        : nextSink(nextSink)
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

            if (!BrotliDecoderDecompressStream(state, &avail_in, &next_in, &avail_out, &next_out, nullptr))
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
    if (method == "none" || method == "" || method == "identity")
        return std::make_unique<NoneSink>(nextSink);
    else if (method == "br")
        return std::make_unique<BrotliDecompressionSink>(nextSink);
    else
        return sourceToSink([method, &nextSink](Source & source) {
            auto decompressionSource = std::make_unique<ArchiveDecompressionSource>(source, method);
            decompressionSource->drainInto(nextSink);
        });
}

struct BrotliCompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    BrotliEncoderState * state;
    bool finished = false;

    BrotliCompressionSink(Sink & nextSink)
        : nextSink(nextSink)
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

            if (!BrotliEncoderCompressStream(
                    state,
                    data.data() ? BROTLI_OPERATION_PROCESS : BROTLI_OPERATION_FINISH,
                    &avail_in,
                    &next_in,
                    &avail_out,
                    &next_out,
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

/**
 * Zstd compression that cuts a new frame every `bytesPerFrame` of
 * uncompressed input.  The result is a concatenation of independent
 * frames, which any conformant zstd decoder (RFC 8878 §3.1) handles
 * transparently — including libarchive's, which is what the nix
 * substituter path uses for decompression.
 *
 * Nix currently decompresses zstd serially, but emitting independent
 * frames with known content sizes now means a future parallel decoder
 * can exploit them without any change to the compressed data on disk.
 *
 * When `parallel` is set, `nbWorkers` is derived from
 * `std::thread::hardware_concurrency()` so per-frame compression can
 * use multiple cores.  `parallel=false` compresses each frame
 * single-threaded but still emits independent frames.
 *
 * Each frame buffers its input and compresses in one shot with an exact
 * `ZSTD_CCtx_setPledgedSrcSize`, so `Frame_Content_Size` is written to
 * every frame header.  A future parallel decoder could compute each
 * frame's output offset by walking headers with
 * `ZSTD_findFrameCompressedSize` + `ZSTD_getFrameContentSize` — no
 * coordination, no assumptions about `bytesPerFrame`.
 *
 * Frame size is fixed at 16 MiB of input.  zstd's window size is
 * level-dependent (~2 MiB at the default level 3, up to 8 MiB at
 * higher levels), so the ratio loss from not being able to reference
 * across a frame boundary is small.  16 MiB gives ~700 frames for the
 * biggest NARs, which is ample parallelism and lets a decoder start
 * work before the whole blob is downloaded.
 */
struct ZstdMultiFrameCompressionSink : CompressionSink
{
    Sink & nextSink;
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> cctx{nullptr, ZSTD_freeCCtx};
    std::vector<char> outbuf;
    /**
     * Input buffer for the current frame.  We accumulate a full
     * frame's worth before compressing so we can set an exact
     * `ZSTD_CCtx_setPledgedSrcSize` — that writes `Frame_Content_Size`
     * into the frame header, which a future parallel decoder could use
     * to compute each frame's output offset without guessing or
     * coordinating.
     *
     * 16 MiB of buffering is negligible next to the multi-GiB
     * compressed output nix already keeps for the FileHash
     * computation.
     */
    std::vector<char> inbuf;
    bool emittedAnyFrame = false;
    static constexpr uint64_t bytesPerFrame = 16 * 1024 * 1024;

    ZstdMultiFrameCompressionSink(Sink & nextSink, bool parallel, int level)
        : nextSink(nextSink)
        , outbuf(ZSTD_CStreamOutSize())
    {
        inbuf.reserve(bytesPerFrame);
        cctx.reset(ZSTD_createCCtx());
        if (!cctx)
            throw CompressionError("unable to initialise zstd encoder");
        if (level != COMPRESSION_LEVEL_DEFAULT)
            checkZstd(ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_compressionLevel, level));
        if (parallel) {
            unsigned ncpu = std::thread::hardware_concurrency();
            /* Cap nbWorkers: zstd's MT engine splits each frame into
               per-worker jobs.  With 16 MiB frames, more than ~4
               workers yields diminishing returns (< 4 MiB per worker)
               and the thread synchronisation overhead can make
               compression slower than single-threaded. */
            if (ncpu > 4)
                ncpu = 4;
            if (ncpu > 1)
                /* Don't checkZstd(): if libzstd was built without
                   ZSTD_MULTITHREAD this returns an error, but per the
                   zstd docs the parameter is simply ignored and
                   compression falls back to single-threaded. */
                ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_nbWorkers, ncpu);
        }
    }

    void checkZstd(size_t ret)
    {
        if (ZSTD_isError(ret))
            throw CompressionError("zstd error: %s", ZSTD_getErrorName(ret));
    }

    /**
     * Compress all of `inbuf` as one complete frame, pledged at its
     * exact size so `Frame_Content_Size` lands in the header.
     */
    void emitFrame()
    {
        checkZstd(ZSTD_CCtx_reset(cctx.get(), ZSTD_reset_session_only));
        checkZstd(ZSTD_CCtx_setPledgedSrcSize(cctx.get(), inbuf.size()));

        ZSTD_inBuffer in = {inbuf.data(), inbuf.size(), 0};
        for (;;) {
            checkInterrupt();
            ZSTD_outBuffer out = {outbuf.data(), outbuf.size(), 0};
            size_t remaining = ZSTD_compressStream2(cctx.get(), &out, &in, ZSTD_e_end);
            checkZstd(remaining);
            if (out.pos > 0)
                nextSink({outbuf.data(), out.pos});
            if (remaining == 0)
                break;
        }
        inbuf.clear();
        emittedAnyFrame = true;
    }

    void writeUnbuffered(std::string_view data) override
    {
        while (!data.empty()) {
            uint64_t room = bytesPerFrame - inbuf.size();
            size_t n = (room < data.size()) ? room : data.size();

            inbuf.insert(inbuf.end(), data.data(), data.data() + n);
            data.remove_prefix(n);

            if (inbuf.size() >= bytesPerFrame)
                emitFrame();
        }
    }

    void finish() override
    {
        flush();
        /* Emit the trailing partial frame, or an empty frame if we
           never wrote anything — the output must contain at least one
           frame header to be valid zstd (otherwise the libarchive
           decoder chokes on round-tripped empty input). */
        if (!inbuf.empty() || !emittedAnyFrame)
            emitFrame();
    }
};

ref<CompressionSink> makeCompressionSink(CompressionAlgo method, Sink & nextSink, const bool parallel, int level)
{
    switch (method) {
    case CompressionAlgo::none:
        return make_ref<NoneSink>(nextSink);
    case CompressionAlgo::brotli:
        return make_ref<BrotliCompressionSink>(nextSink);
    case CompressionAlgo::zstd:
        return make_ref<ZstdMultiFrameCompressionSink>(nextSink, parallel, level);
        /* Everything else is supported via libarchive. */
#define NIX_DEF_LA_ALGO_CASE(algo) case CompressionAlgo::algo:
        NIX_FOR_EACH_LA_ALGO(NIX_DEF_LA_ALGO_CASE)
        return make_ref<ArchiveCompressionSink>(nextSink, method, parallel, level);
#undef NIX_DEF_LA_ALGO_CASE
    }
    unreachable();
}

std::string compress(CompressionAlgo method, std::string_view in, const bool parallel, int level)
{
    StringSource source(in);
    return compress(method, source, parallel, level);
}

std::string compress(CompressionAlgo method, Source & in, const bool parallel, int level)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink, parallel, level);
    in.drainInto(*sink);
    sink->finish();
    return std::move(ssink.s);
}

} // namespace nix
