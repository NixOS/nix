#include "crash-handler.hh"

#include "nix/util/fmt.hh"
#include "nix/util/logging.hh"

#include <boost/core/demangle.hpp>
#include <exception>
#include <sstream>

// Darwin and FreeBSD stdenv do not define _GNU_SOURCE but do have _Unwind_Backtrace.
#if defined(__APPLE__) || defined(__FreeBSD__)
#  define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif

#include <boost/stacktrace/stacktrace.hpp>

#ifndef _WIN32
#  include <syslog.h>
#endif

namespace nix {

namespace {

void logFatal(std::string const & s)
{
    writeToStderr(s + "\n");
    // std::string for guaranteed null termination
#ifndef _WIN32
    syslog(LOG_CRIT, "%s", s.c_str());
#endif
}

void onTerminate()
{
    logFatal(
        "Nix crashed. This is a bug. Please report this at https://github.com/NixOS/nix/issues with the following information included:\n");
    try {
        std::exception_ptr eptr = std::current_exception();
        if (eptr) {
            std::rethrow_exception(eptr);
        } else {
            logFatal("std::terminate() called without exception");
        }
    } catch (const std::exception & ex) {
        logFatal(fmt("Exception: %s: %s", boost::core::demangle(typeid(ex).name()), ex.what()));
    } catch (...) {
        logFatal("Unknown exception!");
    }

    logFatal("Stack trace:");
    std::stringstream ss;
    ss << boost::stacktrace::stacktrace();
    logFatal(ss.str());

    std::abort();
}
} // namespace

void registerCrashHandler()
{
    // DO NOT use this for signals. Boost stacktrace is very much not
    // async-signal-safe, and in a world with ASLR, addr2line is pointless.
    //
    // If you want signals, set up a minidump system and do it out-of-process.
    std::set_terminate(onTerminate);
}
} // namespace nix
