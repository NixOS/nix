#pragma once
///@file

#include <string_view>

namespace nix {

/**
 * Render the given Markdown text to the terminal.
 *
 * If Nix is compiled without Markdown support, this function will return the input text as-is.
 *
 * The renderer takes into account the terminal width, and wraps text accordingly.
 */
std::string renderMarkdownToTerminal(std::string_view markdown);

} // namespace nix
