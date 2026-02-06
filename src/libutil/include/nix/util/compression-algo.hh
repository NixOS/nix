#pragma once
///@file

#include "nix/util/error.hh"

#include <string_view>

namespace nix {

#define NIX_FOR_EACH_COMPRESSION_ALGO(MACRO) \
    MACRO("none", none)                      \
    MACRO("br", brotli)                      \
    MACRO("bzip2", bzip2)                    \
    MACRO("compress", compress)              \
    MACRO("grzip", grzip)                    \
    MACRO("gzip", gzip)                      \
    MACRO("lrzip", lrzip)                    \
    MACRO("lz4", lz4)                        \
    MACRO("lzip", lzip)                      \
    MACRO("lzma", lzma)                      \
    MACRO("lzop", lzop)                      \
    MACRO("xz", xz)                          \
    MACRO("zstd", zstd)

#define NIX_DEFINE_COMPRESSION_ALGO(name, value) value,
enum class CompressionAlgo { NIX_FOR_EACH_COMPRESSION_ALGO(NIX_DEFINE_COMPRESSION_ALGO) };
#undef NIX_DEFINE_COMPRESSION_ALGO

/**
 * Parses a *compression* method into the corresponding enum. This is only used
 * in the *compression* case and user interface. Content-Encoding should not use
 * these.
 *
 * @param suggestions Whether to throw an exception with suggestions.
 */
CompressionAlgo parseCompressionAlgo(std::string_view method, bool suggestions = false);

std::string showCompressionAlgo(CompressionAlgo method);

/**
 * Returns the file extension (including the dot) for the given
 * compression algorithm. Returns empty string for `none`.
 */
std::string_view compressionAlgoExtension(CompressionAlgo method);

MakeError(UnknownCompressionMethod, Error);

} // namespace nix
