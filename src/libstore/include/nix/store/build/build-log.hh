#pragma once
///@file

#include "nix/util/serialise.hh"

#include <functional>
#include <list>
#include <string>

namespace nix {

/**
 * Pure line buffering and log tracking for build output.
 *
 * This class handles:
 * - Tracking log size (for enforcing limits)
 * - Buffering partial lines (handling \r and \n)
 * - Maintaining a tail of recent log lines (for error messages)
 *
 * Implements Sink so it can be used as a data destination.
 * I/O is handled separately by the caller.
 */
struct BuildLog : Sink
{
    /**
     * Callback for complete log lines.
     * @param line The complete log line (without newline)
     * @return true if line was handled as structured JSON (don't add to tail)
     */
    using LineCallback = std::function<bool(std::string_view line)>;

private:
    size_t maxTailLines;
    LineCallback onLine;

    std::list<std::string> logTail;
    std::string currentLogLine;
    size_t currentLogLinePos = 0; // to handle carriage return

    void flushLine();

public:
    /**
     * @param maxTailLines Maximum number of tail lines to keep
     * @param onLine Callback for each complete line
     */
    BuildLog(size_t maxTailLines, LineCallback onLine);

    /**
     * Process output data from child process.
     * Calls the stored callback for each complete line encountered.
     * @param data Raw output data from child
     */
    void operator()(std::string_view data) override;

    /**
     * Flush any remaining partial line.
     * Call this when the child process exits.
     */
    void flush();

    /**
     * Get the most recent log lines.
     * Used for including in error messages.
     */
    const std::list<std::string> & getTail() const
    {
        return logTail;
    }

    /**
     * Check if there's an incomplete line buffered.
     */
    bool hasPartialLine() const
    {
        return !currentLogLine.empty();
    }
};

} // namespace nix
