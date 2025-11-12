#pragma once
///@file

#include "cmark-cpp.hh"

#include <string>
#include <vector>
#include <memory>

namespace cmark {

/**
 * Terminal rendering options
 */
struct TerminalOptions
{
    /** Terminal width in columns */
    size_t cols = 80;

    /** Content width (0 = auto, max 80 or cols) */
    size_t width = 0;

    /** Horizontal margin (left padding) */
    size_t hmargin = 0;

    /** Horizontal padding (additional left padding) */
    size_t hpadding = 4;

    /** Vertical margin (blank lines before/after) */
    size_t vmargin = 0;

    /** Center content */
    bool centre = false;

    /** Disable ANSI escape sequences */
    bool noAnsi = false;

    /** Disable ANSI colors only */
    bool noColor = false;

    /** Don't show any link URLs */
    bool noLink = false;

    /** Don't show relative link URLs */
    bool noRelLink = false;

    /** Shorten long URLs */
    bool shortLink = false;
};

/**
 * Style attributes for terminal output
 */
struct Style
{
    bool italic = false;
    bool strike = false;
    bool bold = false;
    bool under = false;
    size_t bcolour = 0; // Background color (ANSI code)
    size_t colour = 0;  // Foreground color (ANSI code)
    int override = 0;   // Override flags

    static constexpr int OVERRIDE_UNDER = 0x01;
    static constexpr int OVERRIDE_BOLD = 0x02;

    bool hasStyle() const
    {
        return colour || bold || italic || under || strike || bcolour || override;
    }
};

/**
 * Terminal renderer for CommonMark documents
 *
 * Renders a CMark AST to ANSI terminal output with styling, wrapping,
 * and proper indentation.
 */
class TerminalRenderer
{
public:
    /**
     * Create a new terminal renderer with the given options
     */
    explicit TerminalRenderer(const TerminalOptions & opts = TerminalOptions{});

    ~TerminalRenderer();

    // Non-copyable
    TerminalRenderer(const TerminalRenderer &) = delete;
    TerminalRenderer & operator=(const TerminalRenderer &) = delete;

    /**
     * Render a CMark node tree to a string
     */
    std::string render(cmark::Node & root);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/**
 * Convenience function to render a CMark document to terminal output
 */
std::string renderTerminal(cmark::Node & root, const TerminalOptions & opts = TerminalOptions{});

} // namespace cmark
