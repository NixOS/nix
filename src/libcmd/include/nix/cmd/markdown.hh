#pragma once
///@file

#include <string>
#include <string_view>

#include "nix/util/terminal.hh"

namespace nix {

/**
 * Render the given Markdown text to the terminal.
 *
 * If Nix is compiled without Markdown support, this function will return the input text as-is.
 *
 * The renderer takes into account the terminal width, and wraps text accordingly.
 *
 * ANSI styling is kept when \param ansi is set. The default checks stderr; callers that write
 * the result to stdout should pass `shouldANSI(StandardOutputStream::Stdout)` instead.
 */
std::string renderMarkdownToTerminal(std::string_view markdown, bool ansi = shouldANSI());

} // namespace nix
