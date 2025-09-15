#pragma once
/**
 * @file
 *
 * Implementation of some inline definitions for Unix signals, and also
 * some extra Unix-only interfaces.
 *
 * (The only reason everything about signals isn't Unix-only is some
 * no-op definitions are provided on Windows to avoid excess CPP in
 * downstream code.)
 */

#include "nix/util/types.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/ansicolor.hh"
#include "nix/util/signals.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <boost/lexical_cast.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>

namespace nix {

/* User interruption. */

namespace unix {

extern std::atomic<bool> _isInterrupted;

extern thread_local std::function<bool()> interruptCheck;

void _interrupted();

/**
 * Start a thread that handles various signals. Also block those signals
 * on the current thread (and thus any threads created by it).
 * Saves the signal mask before changing the mask to block those signals.
 * See saveSignalMask().
 */
void startSignalHandlerThread();

/**
 * Saves the signal mask, which is the signal mask that nix will restore
 * before creating child processes.
 */
void saveSignalMask();

/**
 * To use in a process that already called `startSignalHandlerThread()`
 * or `saveSignalMask()` first.
 */
void restoreSignals();

void triggerInterrupt();

} // namespace unix

static inline void setInterrupted(bool isInterrupted)
{
    unix::_isInterrupted = isInterrupted;
}

static inline bool getInterrupted()
{
    return unix::_isInterrupted;
}

static inline bool isInterrupted()
{
    using namespace unix;
    return _isInterrupted || (interruptCheck && interruptCheck());
}

/**
 * Throw `Interrupted` exception if the process has been interrupted.
 *
 * Call this in long-running loops and between slow operations to terminate
 * them as needed.
 */
inline void checkInterrupt()
{
    if (isInterrupted())
        unix::_interrupted();
}

/**
 * A RAII class that causes the current thread to receive SIGUSR1 when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 */
struct ReceiveInterrupts
{
    pthread_t target;
    std::unique_ptr<InterruptCallback> callback;

    ReceiveInterrupts()
        : target(pthread_self())
        , callback(createInterruptCallback([&]() { pthread_kill(target, SIGUSR1); }))
    {
    }
};

} // namespace nix
