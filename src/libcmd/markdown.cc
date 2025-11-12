#include "nix/cmd/markdown.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"
#include "nix/util/terminal.hh"

#include <cmark/cmark-cpp.hh>
#include <cmark/cmark-terminal.hh>

namespace nix {

static std::string doRenderMarkdownToTerminal(std::string_view markdown)
{
    int windowWidth = getWindowSize().second;

    // Set up terminal rendering options
    ::cmark::TerminalOptions opts;
    opts.cols = std::max(windowWidth - 5, 60);
    opts.hmargin = 0;
    opts.vmargin = 0;
    opts.noRelLink = true; // Skip rendering relative links

    if (!isTTY())
        opts.noAnsi = true;

    // Parse the markdown document
    auto doc = ::cmark::parse_document(markdown, CMARK_OPT_DEFAULT);
    if (!doc)
        throw Error("cannot parse Markdown document");

    try {
        // Render to terminal
        return ::cmark::renderTerminal(*doc, opts);
    } catch (const std::exception & e) {
        throw Error("error rendering Markdown: %s", e.what());
    }
}

std::string renderMarkdownToTerminal(std::string_view markdown)
{
    if (auto e = getEnv("_NIX_TEST_RAW_MARKDOWN"); e && *e == "1")
        return std::string(markdown);
    else
        return doRenderMarkdownToTerminal(markdown);
}

} // namespace nix
