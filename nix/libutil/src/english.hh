#pragma once

#include <iostream>

namespace nix {

/**
 * Pluralize a given value.
 *
 * If `count == 1`, prints `1 {single}` to `output`, otherwise prints `{count} {plural}`.
 */
std::ostream & pluralize(
    std::ostream & output,
    unsigned int count,
    const std::string_view single,
    const std::string_view plural);

}
