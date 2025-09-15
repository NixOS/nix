#pragma once
///@file

#include <optional>
#include <chrono>

#ifndef _WIN32
#  include <sys/resource.h>
#endif

#include "nix/util/types.hh"

namespace nix {

/**
 * Get the current process's user space CPU time.
 */
std::chrono::microseconds getCpuUserTime();

/**
 * If cgroups are active, attempt to calculate the number of CPUs available.
 * If cgroups are unavailable or if cpu.max is set to "max", return 0.
 */
unsigned int getMaxCPU();

// It does not seem possible to dynamically change stack size on Windows.
#ifndef _WIN32
/**
 * Change the stack size.
 */
void setStackSize(size_t stackSize);
#endif

/**
 * Restore the original inherited Unix process context (such as signal
 * masks, stack size).

 * See unix::startSignalHandlerThread(), unix::saveSignalMask().
 */
void restoreProcessContext(bool restoreMounts = true);

/**
 * @return the path of the current executable.
 */
std::optional<Path> getSelfExe();

} // namespace nix
