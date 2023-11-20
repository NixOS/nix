#pragma once

#include "args.hh"

namespace nix {

/**
 * The concrete implementation of a collection of completions.
 *
 * This is exposed so that the main entry point can print out the
 * collected completions.
 */
struct Completions final : AddCompletions
{
    std::set<Completion> completions;
    Type type = Type::Normal;

    void setType(Type type) override;
    void add(std::string completion, std::string description = "") override;
};

/**
 * The outermost Args object. This is the one we will actually parse a command
 * line with, whereas the inner ones (if they exists) are subcommands (and this
 * is also a MultiCommand or something like it).
 *
 * This Args contains completions state shared between it and all of its
 * descendent Args.
 */
class RootArgs : virtual public Args
{
    /**
     * @brief The command's "working directory", but only set when top level.
     *
     * Use getCommandBaseDir() to get the directory regardless of whether this
     * is a top-level command or subcommand.
     *
     * @see getCommandBaseDir()
     */
    Path commandBaseDir = ".";

public:
    /** Parse the command line, throwing a UsageError if something goes
     * wrong.
     */
    void parseCmdline(const Strings & cmdline, bool allowShebang = false);

    std::shared_ptr<Completions> completions;

    Path getCommandBaseDir() const override;

protected:

    friend class Args;

    /**
     * A pointer to the completion and its two arguments; a thunk;
     */
    struct DeferredCompletion {
        const CompleterClosure & completer;
        size_t n;
        std::string prefix;
    };

    /**
     * Completions are run after all args and flags are parsed, so completions
     * of earlier arguments can benefit from later arguments.
     */
    std::vector<DeferredCompletion> deferredCompletions;

    /**
     * Experimental features needed when parsing args. These are checked
     * after flag parsing is completed in order to support enabling
     * experimental features coming after the flag that needs the
     * experimental feature.
     */
    std::set<ExperimentalFeature> flagExperimentalFeatures;

private:

    std::optional<std::string> needsCompletion(std::string_view s);
};

}
