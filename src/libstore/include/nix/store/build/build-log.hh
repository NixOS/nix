#pragma once
///@file

#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"

#include <list>
#include <map>
#include <string>

namespace nix {

/**
 * Line buffering and log tracking for build output.
 *
 * This class handles:
 * - Owning the build Activity for logging
 * - Buffering partial lines (handling \r and \n)
 * - Maintaining a tail of recent log lines (for error messages)
 * - Processing JSON log messages via handleJSONLogMessage
 *
 * Implements Sink so it can be used as a data destination.
 * I/O is handled separately by the caller.
 */
struct BuildLog : Sink
{
private:
    size_t maxTailLines;

    std::list<std::string> logTail;
    std::string currentLogLine;
    size_t currentLogLinePos = 0; // to handle carriage return

    void flushLine();

public:
    /**
     * The build activity. Owned by BuildLog.
     */
    std::unique_ptr<Activity> act;

    /**
     * Map for tracking nested activities from JSON messages.
     */
    std::map<ActivityId, Activity> builderActivities;

    /**
     * @param maxTailLines Maximum number of tail lines to keep
     * @param act Activity for this build
     */
    BuildLog(size_t maxTailLines, std::unique_ptr<Activity> act);

    /**
     * Process output data from child process.
     * Handles JSON log messages and emits regular lines to activity.
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
