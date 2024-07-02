#pragma once
///@file

#include "types.hh"

#include "curl/curl.h"

namespace nix {
enum struct HttpAuthMethod : unsigned long {
    NONE = CURLAUTH_NONE,
    BASIC = CURLAUTH_BASIC,
    DIGEST = CURLAUTH_DIGEST,
    NEGOTIATE = CURLAUTH_NEGOTIATE,
    NTLM = CURLAUTH_NTLM,
    BEARER = CURLAUTH_BEARER,
    ANY = CURLAUTH_ANY,
    ANYSAFE = CURLAUTH_ANYSAFE
};
}
