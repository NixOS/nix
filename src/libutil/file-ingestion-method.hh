#pragma once

#include <cinttypes>
#include <string>

namespace nix {

/**
 * An enumeration of the main ways we can serialize file system
 * objects.
 */
enum struct FileIngestionMethod : uint8_t {
    /**
     * Flat-file hashing. Directly ingest the contents of a single file
     */
    Flat = 0,
    /**
     * Recursive (or NAR) hashing. Serializes the file-system object in Nix
     * Archive format and ingest that
     */
    Recursive = 1
};

/**
 * Compute the prefix to the hash algorithm which indicates how the
 * files were ingested.
 */
std::string makeFileIngestionPrefix(FileIngestionMethod m);

}
