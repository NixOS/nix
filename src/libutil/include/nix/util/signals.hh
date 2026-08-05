#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/error.hh"
#include "nix/util/fun.hh"
#include "nix/util/logging.hh"

#include <functional>

#if defined(__FreeBSD__)
// SIGUSR1 is used by bdwgc
#  define NIX_SIG_MULTI_INT SIGTSTP
#elif !defined(_WIN32)
#  define NIX_SIG_MULTI_INT SIGUSR1
#endif

namespace nix {

/* User interruption. */

/**
 * @note Does nothing on Windows
 */
static inline void setInterrupted(bool isInterrupted);

/**
 * @note Does nothing on Windows
 */
static inline bool getInterrupted();

/**
 * @note Does nothing on Windows
 */
static inline bool isInterrupted();

/**
 * @note Does nothing on Windows
 */
inline void checkInterrupt();

/**
 * @note Never will happen on Windows
 */
MakeError(Interrupted, BaseError);
MakeError(Cancelled, BaseError);

struct InterruptCallback
{
    virtual ~InterruptCallback();
};

/**
 * Portable identifiers for the signal events that callbacks can be
 * registered for via `createSignalCallback()`.
 */
enum class SignalType {
    /**
     * SIGINT/SIGTERM/SIGHUP, or a programmatic `triggerInterrupt()`.
     */
    Int,
    /**
     * SIGTSTP, called just before the process stops itself.
     */
    Stop,
    /**
     * SIGCONT, i.e. the process was resumed after being stopped.
     */
    Cont,
    /**
     * SIGWINCH, i.e. the terminal size changed.
     */
    Winch,
};

/**
 * Register a function that gets called from the signal handler thread
 * (in a non-signal context) when the given signal type is received.
 *
 * @note Does nothing on Windows
 */
std::unique_ptr<InterruptCallback> createSignalCallback(SignalType type, fun<void()> callback);

/**
 * Register a function that gets called on SIGINT (in a non-signal
 * context).
 *
 * @note Does nothing on Windows
 */
std::unique_ptr<InterruptCallback> createInterruptCallback(fun<void()> callback);

/**
 * A RAII class that causes the current thread to receive NIX_SIG_MULTI_INT when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 *
 * @note Does nothing on Windows
 */
struct ReceiveInterrupts;

} // namespace nix

#include "nix/util/signals-impl.hh"
