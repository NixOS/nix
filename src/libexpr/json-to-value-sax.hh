#pragma once
///@file

#include <memory>
#include <nlohmann/json.hpp>

#include "json-to-value.hh"

/**
 * json_sax and unique_ptr require the inclusion of json.hpp, so this header shall not be included by other headers
 **/

namespace nix {

std::unique_ptr<nlohmann::json_sax<nlohmann::json>> makeJSONSaxParser(EvalState & s, Value & v);

}
