#pragma once
///@file

#include <sched.h>
#include <string>

#include "file-descriptor.hh"
#include "tests/terminal-code-eater.hh"

namespace nix {

struct RunningProcess
{
    pid_t pid;
    Pipe procStdin;
    Pipe procStdout;

    static RunningProcess start(std::string executable, Strings args);
};

/** DFA that catches repl prompts */
class ReplOutputParser
{
public:
    ReplOutputParser(std::string prompt)
        : prompt(prompt)
    {
        assert(!prompt.empty());
    }
    /** Feeds in a character and returns whether this is an open prompt */
    bool feed(char c);

    enum class State {
        Prompt,
        Context,
    };

private:
    State state = State::Prompt;
    size_t pos_in_prompt = 0;
    std::string const prompt;

    void transition(State state, char responsible_char, bool wasPrompt = false);
};

struct TestSession
{
    RunningProcess proc;
    ReplOutputParser outputParser;
    TerminalCodeEater eater;
    std::string outLog;
    std::string prompt;

    TestSession(std::string prompt, RunningProcess && proc)
        : proc(std::move(proc))
        , outputParser(prompt)
        , eater({})
        , outLog({})
        , prompt(prompt)
    {
    }

    bool waitForPrompt();

    void runCommand(std::string command);

    void close();
};
};
