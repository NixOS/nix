#include "windows-error.hh"

#include <error.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix {

std::string WinError::renderError(DWORD lastError)
{
    LPSTR errorText = NULL;

    FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM // use system message tables to retrieve error text
                   |FORMAT_MESSAGE_ALLOCATE_BUFFER // allocate buffer on local heap for error text
                   |FORMAT_MESSAGE_IGNORE_INSERTS,   // Important! will fail otherwise, since we're not  (and CANNOT) pass insertion parameters
                    NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
                    lastError,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&errorText,  // output
                    0, // minimum size for output buffer
                    NULL);   // arguments - see note

    if (NULL != errorText ) {
        std::string s2 { errorText };
        LocalFree(errorText);
        return s2;
    }
    return fmt("CODE=%d", lastError);
}

}
