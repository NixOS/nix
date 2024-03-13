#include <iostream>

#include "test-session.hh"
#include "processes.hh"
#include "tests/debug-char.hh"
#include "util.hh"

namespace nix {

static constexpr const bool DEBUG_REPL_PARSER = false;

RunningProcess RunningProcess::start(std::string executable, Strings args)
{
    args.push_front(executable);

    Pipe procStdin{};
    Pipe procStdout{};

    procStdin.create();
    procStdout.create();

    // This is separate from runProgram2 because we have different IO requirements
    pid_t pid = startProcess([&]() {
        if (dup2(procStdout.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
        if (dup2(procStdin.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        procStdin.writeSide.close();
        procStdout.readSide.close();
        if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
            throw SysError("dupping stderr");
        execvp(executable.c_str(), stringsToCharPtrs(args).data());
        throw SysError("exec did not happen");
    });

    procStdout.writeSide.close();
    procStdin.readSide.close();

    return RunningProcess{
        .pid = pid,
        .procStdin = std::move(procStdin),
        .procStdout = std::move(procStdout),
    };
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif
std::ostream & operator<<(std::ostream & os, ReplOutputParser::State s)
{
    switch (s) {
    case ReplOutputParser::State::Prompt:
        os << "prompt";
        break;
    case ReplOutputParser::State::Context:
        os << "context";
        break;
    }
    return os;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

void ReplOutputParser::transition(State new_state, char responsible_char, bool wasPrompt)
{
    if constexpr (DEBUG_REPL_PARSER) {
        std::cerr << "transition " << new_state << " for " << DebugChar{responsible_char}
                  << (wasPrompt ? " [prompt]" : "") << "\n";
    }
    state = new_state;
    pos_in_prompt = 0;
}

bool ReplOutputParser::feed(char c)
{
    if (c == '\n') {
        transition(State::Prompt, c);
        return false;
    }
    switch (state) {
    case State::Context:
        break;
    case State::Prompt:
        if (pos_in_prompt == prompt.length() - 1 && prompt[pos_in_prompt] == c) {
            transition(State::Context, c, true);
            return true;
        }
        if (pos_in_prompt >= prompt.length() - 1 || prompt[pos_in_prompt] != c) {
            transition(State::Context, c);
            break;
        }
        pos_in_prompt++;
        break;
    }
    return false;
}

/** Waits for the prompt and then returns if a prompt was found */
bool TestSession::waitForPrompt()
{
    std::vector<char> buf(1024);

    for (;;) {
        ssize_t res = read(proc.procStdout.readSide.get(), buf.data(), buf.size());

        if (res < 0) {
            throw SysError("read");
        }
        if (res == 0) {
            return false;
        }

        bool foundPrompt = false;
        for (ssize_t i = 0; i < res; ++i) {
            // foundPrompt = foundPrompt || outputParser.feed(buf[i]);
            bool wasEaten = true;
            eater.feed(buf[i], [&](char c) {
                wasEaten = false;
                foundPrompt = outputParser.feed(buf[i]) || foundPrompt;

                outLog.push_back(c);
            });

            if constexpr (DEBUG_REPL_PARSER) {
                std::cerr << "raw " << DebugChar{buf[i]} << (wasEaten ? " [eaten]" : "") << "\n";
            }
        }

        if (foundPrompt) {
            return true;
        }
    }
}

void TestSession::close()
{
    proc.procStdin.close();
    proc.procStdout.close();
}

void TestSession::runCommand(std::string command)
{
    if constexpr (DEBUG_REPL_PARSER)
        std::cerr << "runCommand " << command << "\n";
    command += "\n";
    // We have to feed a newline into the output parser, since Nix might not
    // give us a newline before a prompt in all cases (it might clear line
    // first, e.g.)
    outputParser.feed('\n');
    // Echo is disabled, so we have to make our own
    outLog.append(command);
    writeFull(proc.procStdin.writeSide.get(), command, false);
}

};
