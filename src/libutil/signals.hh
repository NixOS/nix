#pragma once
///@file

#include "types.hh"
#include "error.hh"
#include "logging.hh"

#include <functional>

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
void setInterruptThrown();

/**
 * @note Does nothing on Windows
 */
inline void checkInterrupt();

/**
 * @note Never will happen on Windows
 */
MakeError(Interrupted, BaseError);


struct InterruptCallback
{
    virtual ~InterruptCallback() { };
};

/**
 * Register a function that gets called on SIGINT (in a non-signal
 * context).
 *
 * @note Does nothing on Windows
 */
std::unique_ptr<InterruptCallback> createInterruptCallback(
    std::function<void()> callback);

/**
 * A RAII class that causes the current thread to receive SIGUSR1 when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 *
 * @note Does nothing on Windows
 */
struct ReceiveInterrupts;

}

#include "signals-impl.hh"
