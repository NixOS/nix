#pragma once
///@file

namespace nix {


static constexpr std::string_view nameRegexStr =
    // This uses a negative lookahead: (?!\.\.?(-|$))
    //   - deny ".", "..", or those strings followed by '-'
    //   - when it's not those, start again at the start of the input and apply the next regex, which is [0-9a-zA-Z\+\-\._\?=]+
    R"((?!\.\.?(-|$))[0-9a-zA-Z\+\-\._\?=]+)";

}
