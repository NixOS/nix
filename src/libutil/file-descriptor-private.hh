#pragma once

#include "nix/util/file-descriptor.hh"

namespace nix {

struct FdSink;

/**
 * Try to copy @p nbytes bytes from @p from fd to @p to fd, starting at @p offset.
 * The intent is to try to use optimized file copies like copy_file_range (on Linux) first
 * and only fall back to the slower copying when the fast path fails or is unsupported.
 *
 * @throws SystemError in case the copy was started, but failed halfway.
 *
 * @return true if the copy succeeded, false if it's unsupported. Partial copies
 *         should be reported as errors.
 *
 * @pre written should be set to 0 before calling this function.
 */
bool tryCopyFdRangeFast(Descriptor from, Descriptor to, off_t offset, size_t nbytes, size_t & written);

} // namespace nix
