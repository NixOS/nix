#pragma once
///@file

#include "nix/util/result.hh"
#include "nix/util/system-error.hh"

#include <gtest/gtest.h>

#include <source_location>

namespace nix::testing {

/**
 * The value of @p result, or else fail the test with its error.
 *
 * This is for results a test needs in order to carry on, such as opening
 * its temporary directory. Results that are what the test is checking
 * should be checked with `ASSERT_*` or `EXPECT_*` directly instead.
 */
template<typename T>
T valueOrFail(Result<T, NativeError> && result, std::source_location loc = std::source_location::current())
{
    return valueOr(std::move(result), [&](NativeError err) {
        NativeSysError e(err, "unexpected failure");
        /* `ASSERT_*` returns from the function it is used in, so cannot be
           used here, where there is no value to return. Instead record the
           failure where we were called from, and throw to stop the test. */
        ADD_FAILURE_AT(loc.file_name(), loc.line()) << e.what();
        throw e;
    });
}

} // namespace nix::testing
