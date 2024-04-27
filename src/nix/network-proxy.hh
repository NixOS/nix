#pragma once
///@file

#include "types.hh"

namespace nix {

/**
 * Environment variables relating to network proxying. These are used by
 * a few misc commands.
 */
constexpr auto networkProxyVariables = {
    // FIXME until https://github.com/llvm/llvm-project/issues/61560 is
    // implemented
    // clang-format off
    "http_proxy",
    "https_proxy",
    "ftp_proxy",
    "all_proxy",
    "HTTP_PROXY",
    "HTTPS_PROXY",
    "FTP_PROXY",
    "ALL_PROXY",
    // clang-format on
};

}
