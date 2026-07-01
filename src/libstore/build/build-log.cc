#include "nix/store/build/build-log.hh"

namespace nix {

BuildLog::BuildLog(size_t maxTailLines, std::unique_ptr<Activity> act)
    : maxTailLines(maxTailLines)
    , act(std::move(act))
{
}

void BuildLog::operator()(std::string_view data)
{
    for (auto c : data) {
        /* Only let a '\r' reset the column if it isn't followed by '\n', so
           "\r\n" acts as a line terminator; defer a char to handle split chunks. */
        if (pendingCR) {
            pendingCR = false;
            if (c != '\n')
                currentLogLinePos = 0;
        }
        if (c == '\r')
            pendingCR = true;
        else if (c == '\n')
            flushLine();
        else {
            if (currentLogLinePos >= currentLogLine.size())
                currentLogLine.resize(currentLogLinePos + 1);
            currentLogLine[currentLogLinePos++] = c;
        }
    }
}

void BuildLog::flush()
{
    if (!currentLogLine.empty())
        flushLine();
}

void BuildLog::flushLine()
{
    // Truncate to actual content (currentLogLinePos may be less than size due to \r)
    currentLogLine.resize(currentLogLinePos);

    if (!handleJSONLogMessage(currentLogLine, *act, builderActivities, "the derivation builder", false)) {
        // Line was not handled as JSON, emit and add to tail
        act->result(resBuildLogLine, currentLogLine);
        logTail.push_back(currentLogLine);
        if (logTail.size() > maxTailLines)
            logTail.pop_front();
    }

    currentLogLine.clear();
    currentLogLinePos = 0;
}

} // namespace nix
