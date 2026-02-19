#include "nix/util/util.hh"
#include "nix/util/fmt.hh"
#include "nix/util/file-path.hh"
#include "nix/util/signals.hh"

#include <array>
#include <iostream>
#include <regex>

#include <sodium.h>
#include <stdint.h>

#ifdef NDEBUG
#  error "Nix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {

void initLibUtil()
{
    // Check that exception handling works. Exception handling has been observed
    // not to work on darwin when the linker flags aren't quite right.
    // In this case we don't want to expose the user to some unrelated uncaught
    // exception, but rather tell them exactly that exception handling is
    // broken.
    // When exception handling fails, the message tends to be printed by the
    // C++ runtime, followed by an abort.
    // For example on macOS we might see an error such as
    // libc++abi: terminating with uncaught exception of type nix::SystemError: error: C++ exception handling is broken.
    // This would appear to be a problem with the way Nix was compiled and/or linked and/or loaded.
    bool caught = false;
    try {
        throwExceptionSelfCheck();
    } catch (const nix::Error & _e) {
        caught = true;
    }
    // This is not actually the main point of this check, but let's make sure anyway:
    assert(caught);

    if (sodium_init() == -1)
        throw Error("could not initialise libsodium");
}

//////////////////////////////////////////////////////////////////////

std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    for (auto & s : ss)
        res.push_back((char *) s.c_str());
    res.push_back(0);
    return res;
}

//////////////////////////////////////////////////////////////////////

static const int64_t conversionNumber = 1024;

SizeUnit getSizeUnit(int64_t value)
{
    auto unit = sizeUnits.begin();
    uint64_t absValue = std::abs(value);
    while (absValue > conversionNumber && unit < sizeUnits.end()) {
        unit++;
        absValue /= conversionNumber;
    }
    return *unit;
}

std::optional<SizeUnit> getCommonSizeUnit(std::initializer_list<int64_t> values)
{
    assert(values.size() > 0);

    auto it = values.begin();
    SizeUnit unit = getSizeUnit(*it);
    it++;

    for (; it != values.end(); it++) {
        if (unit != getSizeUnit(*it)) {
            return std::nullopt;
        }
    }

    return unit;
}

std::string renderSizeWithoutUnit(int64_t value, SizeUnit unit, bool align)
{
    // bytes should also displayed as KiB => 100 Bytes => 0.1 KiB
    auto power = std::max<std::underlying_type_t<SizeUnit>>(1, std::to_underlying(unit));
    double denominator = std::pow(conversionNumber, power);
    double result = (double) value / denominator;
    return fmt(align ? "%6.1f" : "%.1f", result);
}

char getSizeUnitSuffix(SizeUnit unit)
{
    switch (unit) {
#define NIX_UTIL_DEFINE_SIZE_UNIT(name, suffix) \
    case SizeUnit::name:                        \
        return suffix;
        NIX_UTIL_SIZE_UNITS
#undef NIX_UTIL_DEFINE_SIZE_UNIT
    }

    assert(false);
}

std::string renderSize(int64_t value, bool align)
{
    SizeUnit unit = getSizeUnit(value);
    return fmt("%s %ciB", renderSizeWithoutUnit(value, unit, align), getSizeUnitSuffix(unit));
}

void ignoreExceptionInDestructor(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (Error & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.info().msg);
        } catch (std::exception & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.what());
        }
    } catch (...) {
    }
}

void ignoreExceptionExceptInterrupt(Verbosity lvl)
{
    try {
        throw;
    } catch (const Interrupted & e) {
        throw;
    } catch (Error & e) {
        printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.info().msg);
    } catch (std::exception & e) {
        printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.what());
    }
}

} // namespace nix
