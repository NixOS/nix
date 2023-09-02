#pragma once
///@file

#include <errhandlingapi.h>

#include "error.hh"

namespace nix {

/**
 * Windows Error type.
 *
 * Unless you need to catch a specific error number, don't catch this in
 * portable code. Catch `SystemError` instead.
 */
class WinError : public SystemError
{
public:
    DWORD lastError;

    /**
     * Construct using the explicitly-provided error number.
     * `FormatMessageA` will be used to try to add additional
     * information to the message.
     */
    template<typename... Args>
    WinError(DWORD lastError, const Args & ... args)
        : SystemError(""), lastError(lastError)
    {
        auto hf = HintFmt(args...);
        err.msg = HintFmt("%1%: %2%", Uncolored(hf.str()), renderError(lastError));
    }

    /**
     * Construct using `GetLastError()` and the ambient "last error".
     *
     * Be sure to not perform another last-error-modifying operation
     * before calling this constructor!
     */
    template<typename... Args>
    WinError(const Args & ... args)
        : WinError(GetLastError(), args ...)
    {
    }

private:

    std::string renderError(DWORD lastError);
};

}
